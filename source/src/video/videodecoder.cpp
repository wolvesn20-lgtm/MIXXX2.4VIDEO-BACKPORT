#include "video/videodecoder.h"
#include "video/ffmpeg_compat.h"

#include <QFileInfo>
#include <QMutexLocker>
#include <QDebug>

#include <cstring>
#include <cmath>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

#include "control/controlproxy.h"
#include "control/control.h"

VideoDecoder::VideoDecoder(const QString& group, QObject* parent)
        : QThread(parent), m_group(group), m_path(""), m_abort(false) {
    ConfigKey positionKey(m_group, "playposition");
    ConfigKey playKey(m_group, "play");
    m_pPlayPosition = new ControlProxy(positionKey, this);
    m_pPlay = new ControlProxy(playKey, this);
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
    delete m_pPlayPosition;
    delete m_pPlay;
    if (m_pixelData) delete[] m_pixelData;
}

void VideoDecoder::openFile(const QString& path) {
    {
        QMutexLocker locker(&m_mutex);
        m_path = path;
        m_abort = false;
    }
    if (path.isEmpty() || !QFileInfo::exists(path)) return;
    if (!isRunning()) start();
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
    if (path.isEmpty()) return;

    AVFormatContext* formatCtx = nullptr;
    if (avformat_open_input(&formatCtx, path.toUtf8().constData(), nullptr, nullptr) < 0) return;
    if (avformat_find_stream_info(formatCtx, nullptr) < 0) { avformat_close_input(&formatCtx); return; }

    int videoStreamIndex = -1;
    for (unsigned int i = 0; i < formatCtx->nb_streams; ++i) {
        if (formatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            videoStreamIndex = static_cast<int>(i);
            break;
        }
    }
    if (videoStreamIndex < 0) { avformat_close_input(&formatCtx); return; }

    AVCodecParameters* codecParams = formatCtx->streams[videoStreamIndex]->codecpar;
    const AVCodec* codec = avcodec_find_decoder(codecParams->codec_id);
    if (!codec) { avformat_close_input(&formatCtx); return; }

    AVCodecContext* codecCtx = avcodec_alloc_context3(codec);
    if (!codecCtx || avcodec_parameters_to_context(codecCtx, codecParams) < 0) {
        if (codecCtx) avcodec_free_context(&codecCtx);
        avformat_close_input(&formatCtx); return;
    }
    if (avcodec_open2(codecCtx, codec, nullptr) < 0) {
        avcodec_free_context(&codecCtx);
        avformat_close_input(&formatCtx); return;
    }

    m_width = codecCtx->width;
    m_height = codecCtx->height;
    
    {
        QMutexLocker locker(&m_bufferMutex);
        if (m_pixelData) delete[] m_pixelData;
        m_pixelData = new uchar[m_width * m_height * 4];
    }

    SwsContext* swsCtx = sws_getContext(
            m_width, m_height, codecCtx->pix_fmt,
            m_width, m_height, AV_PIX_FMT_BGRA,
            SWS_BILINEAR, nullptr, nullptr, nullptr);

    AVPacket* packet = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    double durationMs = formatCtx->duration / 1000.0; 
    bool aborted = false;
    double lastTargetAudioTimeMs = -1.0;

    while (!aborted) {
        {
            QMutexLocker locker(&m_mutex);
            if (m_abort || m_path != path) { aborted = true; break; }
        }
        if (!m_pPlayPosition || !m_pPlay) { QThread::msleep(10); continue; }
        if (m_pPlay->get() == 0.0) { QThread::msleep(16); continue; }

        double audioTimeMs = m_pPlayPosition->get() * durationMs;

        if (lastTargetAudioTimeMs < 0 || std::abs(audioTimeMs - lastTargetAudioTimeMs) > 1000.0) {
            int64_t targetTimestamp = static_cast<int64_t>((audioTimeMs / 1000.0) / av_q2d(formatCtx->streams[videoStreamIndex]->time_base));
            avcodec_flush_buffers(codecCtx);
            avformat_seek_file(formatCtx, videoStreamIndex, INT64_MIN, targetTimestamp, targetTimestamp, AVSEEK_FLAG_BACKWARD);
            lastTargetAudioTimeMs = audioTimeMs;
        }

        if (av_read_frame(formatCtx, packet) < 0) { QThread::msleep(10); continue; }
        if (packet->stream_index != videoStreamIndex) { av_packet_unref(packet); continue; }
        if (avcodec_send_packet(codecCtx, packet) < 0) { av_packet_unref(packet); continue; }
        av_packet_unref(packet);

        while (avcodec_receive_frame(codecCtx, frame) == 0) {
            double frameTimeMs = 0;
            if (frame->best_effort_timestamp != AV_NOPTS_VALUE) {
                frameTimeMs = frame->best_effort_timestamp * av_q2d(formatCtx->streams[videoStreamIndex]->time_base) * 1000.0;
            }
            lastTargetAudioTimeMs = frameTimeMs;

            if (frameTimeMs > audioTimeMs + 30.0) {
                QThread::msleep(8); av_frame_unref(frame); break; 
            } else if (frameTimeMs < audioTimeMs - 50.0) {
                av_frame_unref(frame); QThread::msleep(2); continue; 
            }

            // Write directly to shared buffer with mutex protection
            {
                QMutexLocker locker(&m_bufferMutex);
                if (m_pixelData && swsCtx) {
                    uint8_t* dstData[4] = {m_pixelData, nullptr, nullptr, nullptr};
                    int dstLinesize[4] = {m_width * 4, 0, 0, 0};
                    sws_scale(swsCtx, frame->data, frame->linesize, 0, m_height, dstData, dstLinesize);
                }
            }
            emit frameReady();
            av_frame_unref(frame);
        }
    }

    if (swsCtx) sws_freeContext(swsCtx);
    av_packet_free(&packet);
    av_frame_free(&frame);
    avcodec_free_context(&codecCtx);
    avformat_close_input(&formatCtx);
}
