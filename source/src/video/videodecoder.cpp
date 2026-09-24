#include "video/videodecoder.h"

#include <QFileInfo>
#include <QMutexLocker>

#ifdef __FFMPEG__
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
}
#endif

VideoDecoder::VideoDecoder(QObject* parent)
        : QThread(parent) {
}

VideoDecoder::~VideoDecoder() {
    close();
}

void VideoDecoder::openFile(const QString& path) {
    close();
    if (!QFileInfo::exists(path)) {
        emit openFailed(QStringLiteral("Video file does not exist: %1").arg(path));
        return;
    }
    {
        QMutexLocker locker(&m_mutex);
        m_path = path;
        m_abort = false;
    }
    start();
}

void VideoDecoder::close() {
    {
        QMutexLocker locker(&m_mutex);
        m_abort = true;
    }
    if (isRunning()) {
        wait();
    }
}

void VideoDecoder::run() {
#ifdef __FFMPEG__
    AVFormatContext* format = nullptr;
    AVCodecContext* codec = nullptr;
    SwsContext* scaler = nullptr;
    AVPacket* packet = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    AVFrame* rgba = av_frame_alloc();
    if (!packet || !frame || !rgba) {
        emit openFailed(QStringLiteral("Unable to allocate FFmpeg state"));
        av_packet_free(&packet);
        av_frame_free(&frame);
        av_frame_free(&rgba);
        return;
    }

    const QString path = m_path;
    if (avformat_open_input(&format, path.toUtf8().constData(), nullptr, nullptr) < 0 ||
            avformat_find_stream_info(format, nullptr) < 0) {
        emit openFailed(QStringLiteral("Unable to open video: %1").arg(path));
        avformat_close_input(&format);
        av_packet_free(&packet);
        av_frame_free(&frame);
        av_frame_free(&rgba);
        return;
    }

    const int streamIndex = av_find_best_stream(
            format, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (streamIndex < 0) {
        emit openFailed(QStringLiteral("No video stream found: %1").arg(path));
        avformat_close_input(&format);
        av_packet_free(&packet);
        av_frame_free(&frame);
        av_frame_free(&rgba);
        return;
    }

    const AVCodecParameters* parameters = format->streams[streamIndex]->codecpar;
    const AVCodec* decoder = avcodec_find_decoder(parameters->codec_id);
    codec = avcodec_alloc_context3(decoder);
    if (!decoder || !codec || avcodec_parameters_to_context(codec, parameters) < 0 ||
            avcodec_open2(codec, decoder, nullptr) < 0) {
        emit openFailed(QStringLiteral("Unable to open video codec: %1").arg(path));
        avcodec_free_context(&codec);
        avformat_close_input(&format);
        av_packet_free(&packet);
        av_frame_free(&frame);
        av_frame_free(&rgba);
        return;
    }

    const int width = codec->width;
    const int height = codec->height;
    QImage image(width, height, QImage::Format_RGB32);
    scaler = sws_getContext(width, height, codec->pix_fmt, width, height,
            AV_PIX_FMT_BGRA, SWS_BILINEAR, nullptr, nullptr, nullptr);
    while (av_read_frame(format, packet) >= 0) {
        {
            QMutexLocker locker(&m_mutex);
            if (m_abort) {
                av_packet_unref(packet);
                break;
            }
        }
        if (packet->stream_index == streamIndex && avcodec_send_packet(codec, packet) >= 0) {
            while (avcodec_receive_frame(codec, frame) >= 0) {
                uint8_t* output[] = {image.bits()};
                int stride[] = {static_cast<int>(image.bytesPerLine())};
                sws_scale(scaler, frame->data, frame->linesize, 0, height,
                        output, stride);
                emit frameDecoded(image.copy());
            }
        }
        av_packet_unref(packet);
    }

    sws_freeContext(scaler);
    avcodec_free_context(&codec);
    avformat_close_input(&format);
    av_packet_free(&packet);
    av_frame_free(&frame);
    av_frame_free(&rgba);
#else
    emit openFailed(QStringLiteral("Mixxx was built without FFmpeg support"));
#endif
}
