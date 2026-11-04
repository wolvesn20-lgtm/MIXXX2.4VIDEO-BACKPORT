#include "video/videodecoder.h"
#include "video/ffmpeg_compat.h"

#include <QFileInfo>
#include <QMutexLocker>
#include <QDebug>

#include <cstring>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

#include "control/controlproxy.h"

namespace {

QImage frameToQImage(const AVFrame* frame, int width, int height) {
    // Full resolution decoding - no artificial limits
    QImage image(width, height, QImage::Format_RGB32);

    SwsContext* sws = sws_getContext(
            width, height, static_cast<AVPixelFormat>(frame->format),
            width, height, AV_PIX_FMT_BGRA,
            SWS_BILINEAR, nullptr, nullptr, nullptr);

    if (!sws) {
        return QImage();
    }

    uint8_t* dstData[4] = {image.bits(), nullptr, nullptr, nullptr};
    int dstLinesize[4] = {static_cast<int>(image.bytesPerLine()), 0, 0, 0};

    sws_scale(sws, frame->data, frame->linesize, 0, height, dstData, dstLinesize);
    sws_freeContext(sws);

    return image;
}

} // namespace

VideoDecoder::VideoDecoder(const QString& group, QObject* parent)
        : QThread(parent), m_group(group) {
    // Use standard playposition (0.0 to 1.0) as the master clock
    m_pPlayPosition = new ControlProxy(m_group, "playposition", this);
}

VideoDecoder::~VideoDecoder() {
    {
        QMutexLocker locker(&m_mutex);
        m_abort = true;
    }
    if (!wait(5000)) {
        terminate();
        wait(1000);
    }
}

void VideoDecoder::openFile(const QString& path) {
    {
        QMutexLocker locker(&m_mutex);
        m_path = path;
        m_abort = false;
    }

    if (path.isEmpty()) {
        emit openFailed(tr("Empty video path"));
        return;
    }
    if (!QFileInfo::exists(path)) {
        emit openFailed(tr("File does not exist: %1").arg(path));
        return;
    }

    if (!isRunning()) {
        start();
    }
}

void VideoDecoder::close() {
    {
        QMutexLocker locker(&m_mutex);
        m_abort = true;
    }
    if (!wait(5000)) {
        terminate();
        wait(1000);
    }
}

void VideoDecoder::run() {
    QString path;
    {
        QMutexLocker locker(&m_mutex);
        path = m_path;
    }

    if (path.isEmpty()) {
        emit openFailed(tr("No video path set"));
        return;
    }

    AVFormatContext* formatCtx = nullptr;
    if (avformat_open_input(&formatCtx, path.toUtf8().constData(), nullptr, nullptr) < 0) {
        emit openFailed(tr("Failed to open video file: %1").arg(path));
        return;
    }

    if (avformat_find_stream_info(formatCtx, nullptr) < 0) {
        emit openFailed(tr("Failed to read video stream info"));
        avformat_close_input(&formatCtx);
        return;
    }

    int videoStreamIndex = -1;
    for (unsigned int i = 0; i < formatCtx->nb_streams; ++i) {
        if (formatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            videoStreamIndex = static_cast<int>(i);
            break;
        }
    }

    if (videoStreamIndex < 0) {
        emit openFailed(tr("No video stream found in file"));
        avformat_close_input(&formatCtx);
        return;
    }

    AVCodecParameters* codecParams = formatCtx->streams[videoStreamIndex]->codecpar;

    const AVCodec* codec = avcodec_find_decoder(codecParams->codec_id);
    if (!codec) {
        emit openFailed(tr("No decoder found for video codec"));
        avformat_close_input(&formatCtx);
        return;
    }

    AVCodecContext* codecCtx = avcodec_alloc_context3(codec);
    if (!codecCtx) {
        emit openFailed(tr("Failed to allocate codec context"));
        avformat_close_input(&formatCtx);
        return;
    }

    if (avcodec_parameters_to_context(codecCtx, codecParams) < 0) {
        emit openFailed(tr("Failed to copy codec parameters"));
        avcodec_free_context(&codecCtx);
        avformat_close_input(&formatCtx);
        return;
    }

    if (avcodec_open2(codecCtx, codec, nullptr) < 0) {
        emit openFailed(tr("Failed to open video codec"));
        avcodec_free_context(&codecCtx);
        avformat_close_input(&formatCtx);
        return;
    }

    AVPacket* packet = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();

    if (!packet || !frame) {
        emit openFailed(tr("Failed to allocate decode buffers"));
        av_packet_free(&packet);
        av_frame_free(&frame);
        avcodec_free_context(&codecCtx);
        avformat_close_input(&formatCtx);
        return;
    }

    // Setup sync variables
    int frameIndex = 0; 
    double fps = av_q2d(formatCtx->streams[videoStreamIndex]->avg_frame_rate);
    if (fps <= 0 || fps > 120) fps = 30.0; // Fallback if fps is broken
    double durationMs = formatCtx->duration / 1000.0; // Convert microseconds to ms

    bool aborted = false;

    while (av_read_frame(formatCtx, packet) >= 0) {
        {
            QMutexLocker locker(&m_mutex);
            if (m_abort) {
                aborted = true;
            }
        }
        if (aborted) {
            av_packet_unref(packet);
            break;
        }

        if (packet->stream_index != videoStreamIndex) {
            av_packet_unref(packet);
            continue;
        }

        if (avcodec_send_packet(codecCtx, packet) < 0) {
            av_packet_unref(packet);
            continue;
        }
        av_packet_unref(packet);

        while (avcodec_receive_frame(codecCtx, frame) == 0) {
            
            // --- THE SYNC ENGINE ---
            if (m_pPlayPosition) {
                double playPos = m_pPlayPosition->get(); // 0.0 to 1.0
                double audioTimeMs = playPos * durationMs;
                
                double frameTimeMs = 0;
                if (frame->best_effort_timestamp != AV_NOPTS_VALUE) {
                    // Normal MP4 with working timestamps
                    frameTimeMs = frame->best_effort_timestamp * av_q2d(formatCtx->streams[videoStreamIndex]->time_base) * 1000.0;
                } else {
                    // Fallback for corrupted MP4s
                    frameTimeMs = (frameIndex * 1000.0) / fps;
                }
                frameIndex++;

                // Sync Logic
                if (frameTimeMs > audioTimeMs + 50) {
                    QThread::msleep(10); // Video is ahead. Wait for audio.
                } else if (frameTimeMs < audioTimeMs - 100) {
                    av_frame_unref(frame); // Video is behind. Drop frame to catch up.
                    continue; 
                }
            }
            // -------------------------------

            QImage image = frameToQImage(frame, codecCtx->width, codecCtx->height);
            if (!image.isNull()) {
                emit frameDecoded(image);
            }
            av_frame_unref(frame);
            
            QThread::msleep(1); 

            {
                QMutexLocker locker(&m_mutex);
                if (m_abort) {
                    aborted = true;
                    break;
                }
            }
        }

        if (aborted) {
            break;
        }
    }

    if (!aborted) {
        avcodec_send_packet(codecCtx, nullptr);
        while (avcodec_receive_frame(codecCtx, frame) == 0) {
            QImage image = frameToQImage(frame, codecCtx->width, codecCtx->height);
            if (!image.isNull()) {
                emit frameDecoded(image);
            }
            av_frame_unref(frame);
        }
    }

    av_packet_free(&packet);
    av_frame_free(&frame);
    avcodec_free_context(&codecCtx);
    avformat_close_input(&formatCtx);
}
