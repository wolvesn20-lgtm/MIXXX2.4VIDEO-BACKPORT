#include "video/videodecoder.h"
#include "video/ffmpeg_compat.h"

#include <QFileInfo>
#include <QMutexLocker>

#include <cstring>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
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

    // Calculate wait time to stop the fast-forward
    int waitTimeMs = 33; // Default to ~30fps
    AVRational frameRate = formatCtx->streams[videoStreamIndex]->avg_frame_rate;
    if (frameRate.den > 0 && frameRate.num > 0) {
        waitTimeMs = 1000 / av_q2d(frameRate);
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
            QImage image = frameToQImage(frame, codecCtx->width, codecCtx->height);
            if (!image.isNull()) {
                emit frameDecoded(image);
            }
            av_frame_unref(frame);
            
            // THE SPEED FIX: Sleep between frames
            QThread::msleep(waitTimeMs);

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
