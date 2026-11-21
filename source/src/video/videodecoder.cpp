#include "video/videodecoder.h"
#include "video/ffmpeg_compat.h"

#include <QDebug>
#include <QFileInfo>
#include <QMutexLocker>
#include <cmath>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

#include "control/control.h"
#include "control/controlproxy.h"

VideoDecoder::VideoDecoder(const QString& group, QObject* parent)
        : QThread(parent),
          m_group(group) {
    m_pPlayPosition = new ControlProxy(ConfigKey(m_group, "playposition"), this);
    m_pPlay = new ControlProxy(ConfigKey(m_group, "play"), this);
    m_pDuration = new ControlProxy(ConfigKey(m_group, "duration"), this);
    m_pVolume = new ControlProxy(ConfigKey(m_group, "volume"), this);
    m_pCrossfader = new ControlProxy(ConfigKey("[Master]", "crossfader"), this);
}

VideoDecoder::~VideoDecoder() {
    m_abort = true;
    if (!wait(1500)) {
        terminate();
        wait(500);
    }
    delete m_pPlayPosition;
    delete m_pPlay;
    delete m_pDuration;
    delete m_pVolume;
    delete m_pCrossfader;
}

void VideoDecoder::openFile(const QString& path) {
    {
        QMutexLocker locker(&m_mutex);
        m_path = path;
        m_abort = false;
        m_reloadRequested = true;
    }

    if (path.isEmpty() || !QFileInfo::exists(path)) {
        return;
    }

    if (!isRunning()) {
        start();
    }
}

void VideoDecoder::close() {
    m_abort = true;
}

bool VideoDecoder::copyLatestFrame(QImage& outImage, uint64_t& lastCounter) {
    m_framePending = false;
    uint64_t current = m_frameCounter.load();
    if (current == lastCounter) {
        return false;
    }

    QMutexLocker lock(&m_bufferMutex);
    if (m_sharedImage.isNull()) {
        return false;
    }
    outImage = m_sharedImage.copy();
    lastCounter = current;
    return true;
}

bool VideoDecoder::scaleFrameToImage(
        SwsContext* swsCtx,
        AVFrame* frame,
        QImage& targetImage,
        int width,
        int height) {
    if (!swsCtx || !frame || width <= 0 || height <= 0) {
        return false;
    }

    if (targetImage.isNull() ||
            targetImage.width() != width ||
            targetImage.height() != height ||
            targetImage.format() != QImage::Format_RGB32) {
        targetImage = QImage(width, height, QImage::Format_RGB32);
    }

    uint8_t* destData[4] = {targetImage.bits(), nullptr, nullptr, nullptr};
    int destLinesize[4] = {static_cast<int>(targetImage.bytesPerLine()), 0, 0, 0};

    int scaledHeight = sws_scale(
            swsCtx,
            frame->data,
            frame->linesize,
            0,
            frame->height,
            destData,
            destLinesize);

    return scaledHeight > 0;
}

void VideoDecoder::run() {
    while (!m_abort) {
        QString currentPath;
        {
            QMutexLocker lock(&m_mutex);
            currentPath = m_path;
            m_reloadRequested = false;
        }

        if (currentPath.isEmpty() || !QFileInfo::exists(currentPath)) {
            msleep(25);
            continue;
        }

        AVFormatContext* formatCtx = nullptr;
        if (avformat_open_input(&formatCtx, currentPath.toUtf8().constData(), nullptr, nullptr) < 0) {
            emit openFailed("Could not open video file container format.");
            msleep(100);
            continue;
        }

        if (avformat_find_stream_info(formatCtx, nullptr) < 0) {
            emit openFailed("Could not read stream context metadata.");
            avformat_close_input(&formatCtx);
            msleep(100);
            continue;
        }

        int videoStreamIdx = -1;
        for (unsigned int i = 0; i < formatCtx->nb_streams; i++) {
            if (formatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
                videoStreamIdx = static_cast<int>(i);
                break;
            }
        }

        if (videoStreamIdx == -1) {
            emit openFailed("No valid video stream track found in file.");
            avformat_close_input(&formatCtx);
            // Wait until a new track is loaded instead of spinning
            while (!m_abort && !m_reloadRequested) {
                msleep(30);
            }
            continue;
        }

        AVCodecParameters* codecPar = formatCtx->streams[videoStreamIdx]->codecpar;
        const AVCodec* codec = avcodec_find_decoder(codecPar->codec_id);
        if (!codec) {
            emit openFailed("Could not find matching video decoder.");
            avformat_close_input(&formatCtx);
            while (!m_abort && !m_reloadRequested) {
                msleep(30);
            }
            continue;
        }

        AVCodecContext* codecCtx = avcodec_alloc_context3(codec);
        avcodec_parameters_to_context(codecCtx, codecPar);
        codecCtx->thread_count = 2;

        if (avcodec_open2(codecCtx, codec, nullptr) < 0) {
            emit openFailed("Could not open video codec.");
            avcodec_free_context(&codecCtx);
            avformat_close_input(&formatCtx);
            while (!m_abort && !m_reloadRequested) {
                msleep(30);
            }
            continue;
        }

        SwsContext* swsCtx = sws_getContext(
                codecCtx->width,
                codecCtx->height,
                codecCtx->pix_fmt,
                codecCtx->width,
                codecCtx->height,
                AV_PIX_FMT_RGB32,
                SWS_BILINEAR,
                nullptr,
                nullptr,
                nullptr);

        if (!swsCtx) {
            emit openFailed("Could not initialize software video scaler.");
            avcodec_free_context(&codecCtx);
            avformat_close_input(&formatCtx);
            while (!m_abort && !m_reloadRequested) {
                msleep(30);
            }
            continue;
        }

        AVPacket* packet = av_packet_alloc();
        AVFrame* frame = av_frame_alloc();
        AVRational timeBase = formatCtx->streams[videoStreamIdx]->time_base;

        double videoDurationSec = 0.0;
        if (formatCtx->streams[videoStreamIdx]->duration != AV_NOPTS_VALUE) {
            videoDurationSec = formatCtx->streams[videoStreamIdx]->duration * av_q2d(timeBase);
        } else if (formatCtx->duration != AV_NOPTS_VALUE) {
            videoDurationSec = static_cast<double>(formatCtx->duration) / AV_TIME_BASE;
        }

        double lastDecodedPtsSec = -1.0;
        bool needInitialFrame = true;

        // Decode loop for the active video file
        while (!m_abort && !m_reloadRequested) {
            // 1. Update live mixer fader opacity (never stops playback/sync!)
            double channelVol = m_pVolume ? m_pVolume->get() : 1.0;
            double crossfaderPos = m_pCrossfader ? m_pCrossfader->get() : 0.0;
            double crossfaderMult = 1.0;
            if (m_group == "[Channel1]") {
                crossfaderMult = (crossfaderPos <= 0.0) ? 1.0 : (1.0 - crossfaderPos);
            } else if (m_group == "[Channel2]") {
                crossfaderMult = (crossfaderPos >= 0.0) ? 1.0 : (1.0 + crossfaderPos);
            }
            m_calculatedOpacity.store(qBound(0.0, channelVol * crossfaderMult, 1.0));

            // 2. Read Mixxx deck transport & playhead position
            bool isPlaying = m_pPlay ? (m_pPlay->get() > 0.0) : false;
            double playPos = m_pPlayPosition ? m_pPlayPosition->get() : 0.0;
            double deckDuration = (m_pDuration && m_pDuration->get() > 0.0)
                                          ? m_pDuration->get()
                                          : videoDurationSec;
            double targetAudioSec = qMax(0.0, playPos * deckDuration);

            // Wrap targetAudioSec if video is shorter than audio track
            if (videoDurationSec > 0.1 && targetAudioSec > videoDurationSec) {
                targetAudioSec = std::fmod(targetAudioSec, videoDurationSec);
            }

            double deltaSec = targetAudioSec - lastDecodedPtsSec;

            // 3. Handle Scratch / Cue Jump / Loop Seek (backward jump or >0.25s forward jump)
            if (!needInitialFrame && lastDecodedPtsSec >= 0.0 &&
                    (deltaSec < -0.04 || deltaSec > 0.25)) {
                int64_t seekTarget = static_cast<int64_t>(targetAudioSec / av_q2d(timeBase));
                av_seek_frame(formatCtx, videoStreamIdx, seekTarget, AVSEEK_FLAG_BACKWARD);
                avcodec_flush_buffers(codecCtx);
                lastDecodedPtsSec = -1.0;
                needInitialFrame = true;
            }

            // 4. Pause & Frame-Pacing Gate (locks video speed 1:1 to audio clock)
            if (!needInitialFrame) {
                if (!isPlaying && std::fabs(deltaSec) < 0.02) {
                    // Deck is paused and playhead hasn't scrubbed: hold current frame
                    msleep(12);
                    continue;
                }
                if (lastDecodedPtsSec >= 0.0 && lastDecodedPtsSec > targetAudioSec + 0.008) {
                    // Video is ahead of audio clock: sleep until audio catches up
                    int waitMs = static_cast<int>((lastDecodedPtsSec - targetAudioSec) * 1000.0);
                    msleep(qBound(1, waitMs, 15));
                    continue;
                }
            }

            // 5. Decode next frame
            int readRes = av_read_frame(formatCtx, packet);
            if (readRes >= 0) {
                if (packet->stream_index == videoStreamIdx) {
                    if (avcodec_send_packet(codecCtx, packet) >= 0) {
                while (avcodec_receive_frame(codecCtx, frame) >= 0) {
                            int64_t bestPts = (frame->best_effort_timestamp != AV_NOPTS_VALUE) ? frame->best_effort_timestamp : frame->pts;
                            double framePtsSec = (bestPts != AV_NOPTS_VALUE) ? (bestPts * av_q2d(timeBase)) : targetAudioSec;
            // 3. Handle Scratch / Cue Jump / Loop Seek (backward jump or >0.25s forward jump)
            if (!needInitialFrame && lastDecodedPtsSec >= 0.0 &&
                    (deltaSec < -0.04 || deltaSec > 0.25)) {
                int64_t seekTarget = static_cast<int64_t>(targetAudioSec / av_q2d(timeBase));
                av_seek_frame(formatCtx, videoStreamIdx, seekTarget, AVSEEK_FLAG_BACKWARD);
                avcodec_flush_buffers(codecCtx);
                lastDecodedPtsSec = -1.0;
                needInitialFrame = true;
            }

            // 4. Pause & Frame-Pacing Gate (locks video speed 1:1 to audio clock)
            if (!needInitialFrame) {
                if (!isPlaying && std::fabs(deltaSec) < 0.02) {
                    // Deck is paused and playhead hasn't scrubbed: hold current frame
                    msleep(12);
                    continue;
                }
                if (lastDecodedPtsSec >= 0.0 && lastDecodedPtsSec > targetAudioSec + 0.008) {
                    // Video is ahead of audio clock: sleep until audio catches up
                    int waitMs = static_cast<int>((lastDecodedPtsSec - targetAudioSec) * 1000.0);
                    msleep(qBound(1, waitMs, 15));
                    continue;
                }
            }

            // 5. Decode next frame
            int readRes = av_read_frame(formatCtx, packet);
            if (readRes >= 0) {
                if (packet->stream_index == videoStreamIdx) {
                    if (avcodec_send_packet(codecCtx, packet) >= 0) {
                        while (avcodec_receive_frame(codecCtx, frame) >= 0) {
                                                        int64_t bestPts = (frame->best_effort_timestamp != AV_NOPTS_VALUE) ? frame->best_effort_timestamp : frame->pts;
                            double framePtsSec = (bestPts != AV_NOPTS_VALUE) ? (bestPts * av_q2d(timeBase)) : targetAudioSec;

                            // Skip fast-forwarding intermediate frames during a seek
                            if (!needInitialFrame && framePtsSec < targetAudioSec - 0.04) {
                                continue;
                            }

                            lastDecodedPtsSec = framePtsSec;
                            needInitialFrame = false;

                            {
                                QMutexLocker bufferLock(&m_bufferMutex);
                                scaleFrameToImage(
                                        swsCtx,
                                        frame,
                                        m_sharedImage,
                                        codecCtx->width,
                                        codecCtx->height);
                            }

                            m_frameCounter++;
                            if (!m_framePending.exchange(true)) {
                                emit frameReady();
                            }
                            break;
                        }
                    }
                }
                av_packet_unref(packet);
            } else {
                // EOF reached: rewind cleanly if track is still playing
                av_seek_frame(formatCtx, videoStreamIdx, 0, AVSEEK_FLAG_BACKWARD);
                avcodec_flush_buffers(codecCtx);
                lastDecodedPtsSec = -1.0;
                msleep(10);
            }
        }

        // Clean up current file resources before reloading next track or exiting
        av_frame_free(&frame);
        av_packet_free(&packet);
        sws_freeContext(swsCtx);
        avcodec_free_context(&codecCtx);
        avformat_close_input(&formatCtx);
    }
}

