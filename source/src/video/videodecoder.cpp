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
    // 1. FFMPEG ARCHITECTURE INITIALIZATION
    AVFormatContext* formatCtx = avformat_alloc_context();
    if (avformat_open_input(&formatCtx, m_path.toUtf8().constData(), nullptr, nullptr) < 0) {
        emit openFailed("Could not open video file container format.");
        return;
    }

    if (avformat_find_stream_info(formatCtx, nullptr) < 0) {
        emit openFailed("Could not read stream context metadata.");
        avformat_close_input(&formatCtx);
        return;
    }

    int videoStreamIdx = -1;
    for (unsigned int i = 0; i < formatCtx->nb_streams; i++) {
        if (formatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            videoStreamIdx = i;
            break;
        }
    }

    if (videoStreamIdx == -1) {
        emit openFailed("No valid video stream track track found in file.");
        avformat_close_input(&formatCtx);
        return;
    }

    AVCodecParameters* codecPar = formatCtx->streams[videoStreamIdx]->codecpar;
    const AVCodec* codec = avcodec_find_decoder(codecPar->codec_id);
    AVCodecContext* codecCtx = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(codecCtx, codecPar);

    if (avcodec_open2(codecCtx, codec, nullptr) < 0) {
        emit openFailed("Could not instantiate hardware or software codec engine initialization.");
        avcodec_free_context(&codecCtx);
        avformat_close_input(&formatCtx);
        return;
    }

    // Initialize software scaler mapping YUV planes to local RGB32 memory space cleanly
    SwsContext* swsCtx = sws_getContext(
        codecCtx->width, codecCtx->height, codecCtx->pix_fmt,
        codecCtx->width, codecCtx->height, AV_PIX_FMT_RGB32,
        SWS_BILINEAR, nullptr, nullptr, nullptr
    );

    AVPacket* packet = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();

    // Track total length metrics if metadata stream exists safely
    double totalDurationSeconds = 0.0;
    if (formatCtx->streams[videoStreamIdx]->duration != AV_NOPTS_VALUE) {
        totalDurationSeconds = formatCtx->streams[videoStreamIdx]->duration * 
                               av_q2d(formatCtx->streams[videoStreamIdx]->time_base);
    } else if (formatCtx->duration != AV_NOPTS_VALUE) {
        totalDurationSeconds = static_cast<double>(formatCtx->duration) / AV_TIME_BASE;
    }

    // 2. THE MAIN LOOP PIPELINE EXECUTOR
    while (true) {
        {
            QMutexLocker lock(&m_mutex);
            if (m_abort) break;
        }

        // A. DYNAMIC LIVE MIXER FADER REGULATION
        double channelVol = m_pVolume ? m_pVolume->get() : 1.0;
        double crossfaderPos = m_pCrossfader ? m_pCrossfader->get() : 0.0;
        double crossfaderMultiplier = 1.0;

        if (m_group == "[Channel1]") {
            crossfaderMultiplier = (crossfaderPos <= 0.0) ? 1.0 : (1.0 - crossfaderPos);
        } else if (m_group == "[Channel2]") {
            crossfaderMultiplier = (crossfaderPos >= 0.0) ? 1.0 : (1.0 + crossfaderPos);
        }

        m_calculatedOpacity = channelVol * crossfaderMultiplier;

        // Optimization: If faders mean this channel is hidden, completely idle processing
        if (m_calculatedOpacity <= 0.001) {
            msleep(15); 
            continue; 
        }

        // B. TIME-BASED DECODER THROTTLING CONTROLS (Prevents 100% CPU lockup slideshow)
        if (m_pPlayPosition && m_pPlay) {
            bool isPlaying = m_pPlay->get() > 0.0;
            if (isPlaying && frame->pts != AV_NOPTS_VALUE) {
                double currentFrameTime = frame->pts * av_q2d(formatCtx->streams[videoStreamIdx]->time_base);
                double currentAudioTime = m_pPlayPosition->get() * totalDurationSeconds;

                // If decoder is getting ahead of the playing track audio clock, force a light sleep window
                if (currentFrameTime > currentAudioTime + 0.03) {
                    int delayMs = static_cast<int>((currentFrameTime - currentAudioTime) * 1000);
                    if (delayMs > 0) {
                        msleep(qMin(delayMs, 25)); // Cap sleep intervals to preserve scratch tracking response
                    }
                }
            }
        }

        // C. PACKET FETCHING AND DECODING LAYER
        if (av_read_frame(formatCtx, packet) >= 0) {
            if (packet->stream_index == videoStreamIdx) {
                if (avcodec_send_packet(codecCtx, packet) >= 0) {
                    while (avcodec_receive_frame(codecCtx, frame) >= 0) {
                        
                        // D. ALLOCATE OR RESIZE DYNAMIC HARDWARE BUFFERS
                        {
                            QMutexLocker bufferLock(&m_bufferMutex);
                            if (m_width != codecCtx->width || m_height != codecCtx->height) {
                                m_width = codecCtx->width;
                                m_height = codecCtx->height;
                                delete[] m_pixelData;
                                m_pixelData = new uchar[m_width * m_height * 4];
                            }
                        }

                        // E. ZERO-COPY COLOR CONVERSION & ATOMIC COUNTER INCREMENTATION
                        {
                            QMutexLocker bufferLock(&m_bufferMutex);
                            QImage targetImage(m_pixelData, m_width, m_height, QImage::Format_RGB32);
                            
                            if (scaleFrameToImage(swsCtx, frame, targetImage, m_width, m_height)) {
                                m_frameCounter++; // <--- INCREMENT HERE: Signifies brand new pixel array data is ready
                            }
                        }

                        // F. LIGHTWEIGHT 0-BYTE SIGNAL TIMING PING
                        emit frameReady();
                    }
                }
            }
            av_packet_unref(packet);
        } else {
            // End of video reached or stream looping point trigger handling block
            if (m_pPlayPosition && m_pPlayPosition->get() < 0.01) {
                av_seek_frame(formatCtx, videoStreamIdx, 0, AVSEEK_FLAG_BACKWARD);
                avcodec_flush_buffers(codecCtx);
            }
            msleep(10);
        }
    }

    // 3. PIPELINE MEMORY TEARDOWN CLEANUP DESTRUCTION
    av_packet_free(&packet);
    av_frame_free(&frame);
    sws_freeContext(swsCtx);
    avcodec_free_context(&codecCtx);
    avformat_close_input(&formatCtx);
}
