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
        emit openFailed(tr("Failed to open file: %1").arg(path));
        return;
    }

    if (avformat_find_stream_info(formatCtx, nullptr) < 0) {
        emit openFailed(tr("Failed to read stream info"));
        avformat_close_input(&formatCtx);
        return;
    }

    // Identify if a video stream exists inside the container file
    int videoStreamIndex = -1;
    for (unsigned int i = 0; i < formatCtx->nb_streams; ++i) {
        if (formatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            videoStreamIndex = static_cast<int>(i);
            break;
        }
    }

    // --- CONDITION: PURE AUDIO TRUCKING OR UNSUPPORTED FORMAT FALLBACK ---
    if (videoStreamIndex < 0) {
        // Load your default Phoenix background canvas texture
        QString brandingPath = ":/images/branding_default.png"; 
        QImage brandingImg(brandingPath);
        if (brandingImg.isNull()) {
            brandingImg.load("res/images/branding_default.png");
        }

        double durationMs = formatCtx->duration / 1000.0;
        bool aborted = false;

        while (!aborted) {
            {
                QMutexLocker locker(&m_mutex);
                if (m_abort || m_path != path) { 
                    break; // Immediate termination if track changes or deck clears
                }
            }

            if (!m_pPlayPosition || !m_pPlay) {
                QThread::msleep(10);
                continue;
            }

            // Emit the branding logo so the window stays active and clean
            if (!brandingImg.isNull()) {
                emit frameDecoded(brandingImg);
            }

            // High efficiency throttled evaluation loop while playing pure audio tracks
            QThread::msleep(33); // Smooth ~30Hz polling interval
        }

        avformat_close_input(&formatCtx);
        return; // Safe termination of the fallback loop state
    }

    // --- STREAM CONTAINS VALID VIDEO DATA: INITIALIZE FFMPEG CODEC ---
    AVCodecParameters* codecParams = formatCtx->streams[videoStreamIndex]->codecpar;
    const AVCodec* codec = avcodec_find_decoder(codecParams->codec_id);
    if (!codec) {
        emit openFailed(tr("No decoder found for video codec"));
        avformat_close_input(&formatCtx);
        return;
    }

    AVCodecContext* codecCtx = avcodec_alloc_context3(codec);
    if (!codecCtx || avcodec_parameters_to_context(codecCtx, codecParams) < 0) {
        emit openFailed(tr("Failed to prepare codec context"));
        if (codecCtx) avcodec_free_context(&codecCtx);
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
    
    double fps = av_q2d(formatCtx->streams[videoStreamIndex]->avg_frame_rate);
    if (fps <= 0 || fps > 120) fps = 30.0; 
    double durationMs = formatCtx->duration / 1000.0; 

    SwsContext* swsCtx = sws_getContext(
            codecCtx->width, codecCtx->height, codecCtx->pix_fmt,
            codecCtx->width, codecCtx->height, AV_PIX_FMT_BGRA,
            SWS_BILINEAR, nullptr, nullptr, nullptr);

    bool aborted = false;
    double lastTargetAudioTimeMs = -1.0;

    while (!aborted) {
        {
            QMutexLocker locker(&m_mutex);
            if (m_abort || m_path != path) {
                aborted = true;
                break;
            }
        }

        if (!m_pPlayPosition || !m_pPlay) {
            QThread::msleep(10);
            continue;
        }

        if (m_pPlay->get() == 0.0) {
            QThread::msleep(16); 
            continue;
        }

        double playPos = m_pPlayPosition->get(); 
        double audioTimeMs = playPos * durationMs;

        if (lastTargetAudioTimeMs < 0 || std::abs(audioTimeMs - lastTargetAudioTimeMs) > 1000.0) {
            int64_t targetTimestamp = static_cast<int64_t>((audioTimeMs / 1000.0) / av_q2d(formatCtx->streams[videoStreamIndex]->time_base));
            avcodec_flush_buffers(codecCtx);
            avformat_seek_file(formatCtx, videoStreamIndex, INT64_MIN, targetTimestamp, targetTimestamp, AVSEEK_FLAG_BACKWARD);
            lastTargetAudioTimeMs = audioTimeMs;
        }

        if (av_read_frame(formatCtx, packet) < 0) {
            QThread::msleep(10);
            continue;
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
            double frameTimeMs = 0;
            if (frame->best_effort_timestamp != AV_NOPTS_VALUE) {
                frameTimeMs = frame->best_effort_timestamp * av_q2d(formatCtx->streams[videoStreamIndex]->time_base) * 1000.0;
            }

            lastTargetAudioTimeMs = frameTimeMs;

            if (frameTimeMs > audioTimeMs + 30.0) {
                QThread::msleep(8); 
                av_frame_unref(frame);
                break; 
            } else if (frameTimeMs < audioTimeMs - 50.0) {
                av_frame_unref(frame); 
                continue; 
            }

            QImage image(codecCtx->width, codecCtx->height, QImage::Format_RGB32);
            if (scaleFrameToImage(swsCtx, frame, image, codecCtx->width, codecCtx->height)) {
                if (!image.isNull()) {
                    emit frameDecoded(image);
                }
            }
            
            av_frame_unref(frame);

            {
                QMutexLocker locker(&m_mutex);
                if (m_abort || m_path != path) {
                    aborted = true;
                    break;
                }
            }
        }
    }

    if (swsCtx) sws_freeContext(swsCtx);
    av_packet_free(&packet);
    av_frame_free(&frame);
    avcodec_free_context(&codecCtx);
    avformat_close_input(&formatCtx);
}
