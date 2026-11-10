#include "video/videowidget.h"
#include <QPainter>
#include <QMutexLocker>

// Explicit constructor required by Mixxx's skin parser factory architecture
VideoWidget::VideoWidget(QWidget* parent) : QWidget(parent) {
    // Optimization: tells the Qt paint engine that this widget draws its entire 
    // canvas area, avoiding unnecessary background window clear steps.
    setAttribute(Qt::WA_OpaquePaintEvent); 
}

void VideoWidget::setDecoder(VideoDecoder* decoder) {
    m_pDecoder = decoder;
    if (m_pDecoder) {
        // Safe queued connection ensures thread separation between decoder and UI paint ticks
        connect(m_pDecoder, &VideoDecoder::frameReady, this, &VideoWidget::slotFrameReady, Qt::QueuedConnection);
    }
}

void VideoWidget::slotFrameReady() {
    if (!m_pDecoder) return;

    // Read the atomic frame generation metric
    uint64_t currentFrameCount = m_pDecoder->getFrameCounter();
    if (currentFrameCount == m_lastRenderedFrame) {
        return; // Stale data bypass: exits immediately if a new frame hasn't landed
    }

    QMutex* decoderMutex = m_pDecoder->getBufferMutex();
    int width = m_pDecoder->getImageWidth();
    int height = m_pDecoder->getImageHeight();
    uchar* rawPixels = m_pDecoder->getPixelData();

    if (!rawPixels || width <= 0 || height <= 0) return;

    // Fast CPU-side memory snapshot extraction step
    {
        QMutexLocker lock(decoderMutex);
        QImage sharedWrapper(rawPixels, width, height, QImage::Format_RGB32);
        m_uiFrame = sharedWrapper.copy(); // Snapshot isolation lock loop execution
        m_lastRenderedFrame = currentFrameCount; // Pin current execution tracking node
    }

    update(); // Instructs Qt window layer to queue up a paintEvent cycle
}

void VideoWidget::paintEvent(QPaintEvent* event) {
    QPainter painter(this);
    
    if (m_uiFrame.isNull()) {
        painter.fillRect(rect(), Qt::black); // Safe black canvas fallback state
        return;
    }

    // 1. LAYOUT ASPECT RATIO CALCULATOR (Prevents stretching inside LateNight grid rows)
    QSize frameSize = m_uiFrame.size();
    QRect targetRect = rect();
    QSize scaledSize = frameSize.scaled(targetRect.size(), Qt::KeepAspectRatio);
    
    // Offset margins to maintain exact spatial layout centering alignments
    int x = (targetRect.width() - scaledSize.width()) / 2;
    int y = (targetRect.height() - scaledSize.height()) / 2;
    QRect centeredLayoutRect(x, y, scaledSize.width(), scaledSize.height());

    // 2. MIXER FADER OPACITY RENDERING ADJUSTMENT
    double opacity = m_pDecoder ? m_pDecoder->getCalculatedOpacity() : 1.0;
    painter.setOpacity(opacity);

    // 3. FAST SURFACE IMAGING BLIT
    painter.drawImage(centeredLayoutRect, m_uiFrame);
}

// FIX: Explicitly handles automatic video file loading when a track is pushed to the deck row
void VideoWidget::slotLoadTrack(TrackPointer pTrack) {
    if (m_pDecoder && pTrack) {
        // Pull the clean filepath directly from the track metadata properties
        m_pDecoder->openFile(pTrack->getLocation());
    }
}
