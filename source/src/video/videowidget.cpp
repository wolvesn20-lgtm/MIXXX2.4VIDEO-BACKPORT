#include "video/videowidget.h"
#include <QPainter>
#include <QMutexLocker>

// FIX: Direct the initialization lists to capture the parsed values correctly
VideoWidget::VideoWidget(const QString& group, const SkinContext& context) 
        : QWidget(nullptr), m_group(group) {
    Q_UNUSED(context); 
    setAttribute(Qt::WA_OpaquePaintEvent); 
}

void VideoWidget::setDecoder(VideoDecoder* decoder) {
    m_pDecoder = decoder;
    if (m_pDecoder) {
        connect(m_pDecoder, &VideoDecoder::frameReady, this, &VideoWidget::slotFrameReady, Qt::QueuedConnection);
    }
}

void VideoWidget::slotFrameReady() {
    if (!m_pDecoder) return;

    uint64_t currentFrameCount = m_pDecoder->getFrameCounter();
    if (currentFrameCount == m_lastRenderedFrame) {
        return; 
    }

    QMutex* decoderMutex = m_pDecoder->getBufferMutex();
    int width = m_pDecoder->getImageWidth();
    int height = m_pDecoder->getImageHeight();
    uchar* rawPixels = m_pDecoder->getPixelData();

    if (!rawPixels || width <= 0 || height <= 0) return;

    {
        QMutexLocker lock(decoderMutex);
        QImage sharedWrapper(rawPixels, width, height, QImage::Format_RGB32);
        m_uiFrame = sharedWrapper.copy(); 
        m_lastRenderedFrame = currentFrameCount; 
    }

    update(); 
}

void VideoWidget::paintEvent(QPaintEvent* event) {
    QPainter painter(this);
    
    if (m_uiFrame.isNull()) {
        painter.fillRect(rect(), Qt::black); 
        return;
    }

    QSize frameSize = m_uiFrame.size();
    QRect targetRect = rect();
    QSize scaledSize = frameSize.scaled(targetRect.size(), Qt::KeepAspectRatio);
    
    int x = (targetRect.width() - scaledSize.width()) / 2;
    int y = (targetRect.height() - scaledSize.height()) / 2;
    QRect centeredLayoutRect(x, y, scaledSize.width(), scaledSize.height());

    double opacity = m_pDecoder ? m_pDecoder->getCalculatedOpacity() : 1.0;
    painter.setOpacity(opacity);

    painter.drawImage(centeredLayoutRect, m_uiFrame);
}

void VideoWidget::slotLoadTrack(TrackPointer pTrack) {
    if (m_pDecoder && pTrack) {
        m_pDecoder->openFile(pTrack->getLocation());
    }
}

// Ensure the automated moc layout compilation links the changes smoothly
#include "videowidget.moc"
