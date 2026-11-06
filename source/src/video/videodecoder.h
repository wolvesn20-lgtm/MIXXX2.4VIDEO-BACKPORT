#include "video/videowidget.h"
#include <QPainter>
#include <QMutexLocker>

VideoWidget::VideoWidget(QWidget* parent) : QWidget(parent) {
    setAttribute(Qt::WA_OpaquePaintEvent); // Optimization: tells Qt widget background clearing isn't forced
}

void VideoWidget::setDecoder(VideoDecoder* decoder) {
    m_pDecoder = decoder;
    if (m_pDecoder) {
        connect(m_pDecoder, &VideoDecoder::frameReady, this, &VideoWidget::slotFrameReady, Qt::QueuedConnection);
    }
}

void VideoWidget::slotFrameReady() {
    if (!m_pDecoder) return;

    QMutex* decoderMutex = m_pDecoder->getBufferMutex();
    int width = m_pDecoder->getImageWidth();
    int height = m_pDecoder->getImageHeight();
    uchar* rawPixels = m_pDecoder->getPixelData();

    if (!rawPixels || width <= 0 || height <= 0) return;

    // Fast local memory snapshot copy from the worker thread's buffer
    {
        QMutexLocker lock(decoderMutex);
        QImage sharedWrapper(rawPixels, width, height, QImage::Format_RGB32);
        m_uiFrame = sharedWrapper.copy(); // Fast CPU line-by-line memory copy snapshot
    }

    update(); // Schedules standard paint refresh call execution
}

void VideoWidget::paintEvent(QPaintEvent* event) {
    QPainter painter(this);
    
    if (m_uiFrame.isNull()) {
        painter.fillRect(rect(), Qt::black); // Display clean fallback container canvas if empty
        return;
    }

    // 1. LAYOUT ASPECT RATIO FIX (Prevents distortion inside LateNight skin container layout)
    QSize frameSize = m_uiFrame.size();
    QRect targetRect = rect();
    QSize scaledSize = frameSize.scaled(targetRect.size(), Qt::KeepAspectRatio);
    
    // Position bounding box accurately at center offsets
    int x = (targetRect.width() - scaledSize.width()) / 2;
    int y = (targetRect.height() - scaledSize.height()) / 2;
    QRect centeredLayoutRect(x, y, scaledSize.width(), scaledSize.height());

    // 2. FADER LIVE OPACITY LAYER REGULATION
    double opacity = m_pDecoder ? m_pDecoder->getCalculatedOpacity() : 1.0;
    painter.setOpacity(opacity);

    // 3. FAST FRAME BLITTING EXECUTION
    painter.drawImage(centeredLayoutRect, m_uiFrame);
}
