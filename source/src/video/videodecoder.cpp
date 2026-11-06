#include "video/videodecoder.h"
#include <QMutexLocker>

VideoDecoder::VideoDecoder(const QString& group, QObject* parent)
        : QThread(parent), m_group(group) {
    m_pPlayPosition = new ControlProxy(m_group, "playposition", this);
    m_pPlay = new ControlProxy(m_group, "play", this);
    m_pVolume = new ControlProxy(m_group, "volume", this);
    m_pCrossfader = new ControlProxy("[Master]", "crossfader", this);
}

VideoDecoder::~VideoDecoder() {
    close();
    delete[] m_pixelData;
}

void VideoDecoder::openFile(const QString& path) {
    QMutexLocker lock(&m_mutex);
    m_path = path;
    if (!isRunning()) {
        m_abort = false;
        start();
    }
}

void VideoDecoder::close() {
    {
        QMutexLocker lock(&m_mutex);
        m_abort = true;
    }
    wait(); // Ensure the thread safely exits execution loop
}

void VideoDecoder::run() {
    // [Your standard FFmpeg initialization code here: AVFormatContext, AVCodecContext, etc.]
    // Make sure 'codecCtx' and 'swsCtx' are fully initialized targets

    while (true) {
        {
            QMutexLocker lock(&m_mutex);
            if (m_abort) break;
        }

        // 1. DYNAMIC FADER CALCULATION
        double channelVol = m_pVolume ? m_pVolume->get() : 1.0;
        double crossfaderPos = m_pCrossfader ? m_pCrossfader->get() : 0.0;
        double crossfaderMultiplier = 1.0;

        if (m_group == "[Channel1]") {
            crossfaderMultiplier = (crossfaderPos <= 0.0) ? 1.0 : (1.0 - crossfaderPos);
        } else if (m_group == "[Channel2]") {
            crossfaderMultiplier = (crossfaderPos >= 0.0) ? 1.0 : (1.0 + crossfaderPos);
        }

        m_calculatedOpacity = channelVol * crossfaderMultiplier;

        // Optimization: If faders mean this channel is invisible, skip heavy decoding steps
        if (m_calculatedOpacity <= 0.001) {
            msleep(15); 
            continue; 
        }

        // [Your loop frame decoding section: av_read_frame, avcodec_send_packet, avcodec_receive_frame]
        // Assuming 'frame' is successfully received from the decoder context:

        // 2. BUFFER ALLOCATION AND LOCKING
        {
            QMutexLocker bufferLock(&m_bufferMutex);
            if (m_width != codecCtx->width || m_height != codecCtx->height) {
                m_width = codecCtx->width;
                m_height = codecCtx->height;
                delete[] m_pixelData;
                m_pixelData = new uchar[m_width * m_height * 4]; // Allocate memory space for RGB32
            }
        }

        // 3. ZERO-COPY SCALE AND CONVERT DIRECTLY INTO SHARED DATA BUFFER
        {
            QMutexLocker bufferLock(&m_bufferMutex);
            // Constructing the image wrapper directly inside raw buffer space (no extraction copies)
            QImage targetImage(m_pixelData, m_width, m_height, QImage::Format_RGB32);
            
            // Assume scaleFrameToImage operates over swsCtx and populates targetImage memory region
            scaleFrameToImage(swsCtx, frame, targetImage, m_width, m_height);
        }

        // 4. LIGHTWEIGHT UI THREAD SIGNAL NOTIFICATION TRIGGER
        emit frameReady();
        
        // Emulate frame rate throttling or match loop timing to audio position ticks here
    }
}
