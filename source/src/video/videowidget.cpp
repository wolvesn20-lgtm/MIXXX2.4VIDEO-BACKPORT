#include "video/videowidget.h"
#include <QPainter>
#include <QTimer>

VideoWidget::VideoWidget(const QString& group, QWidget* parent)
        : QWidget(parent), m_group(group) {
    setAttribute(Qt::WA_OpaquePaintEvent);

    // Lightweight timer so fader/crossfader opacity changes stay smooth even when paused
    QTimer* pFaderTimer = new QTimer(this);
    connect(pFaderTimer, &QTimer::timeout, this, QOverload<>::of(&VideoWidget::update));
    pFaderTimer->start(33); // ~30 Hz UI opacity refresh
}

void VideoWidget::setDecoder(VideoDecoder* decoder) {
    m_pDecoder = decoder;
    if (m_pDecoder) {
        connect(m_pDecoder,
                &VideoDecoder::frameReady,
                this,
                &VideoWidget::slotFrameReady,
                Qt::QueuedConnection);
    }
}

void VideoWidget::slotFrameReady() {
    if (!m_pDecoder) {
        return;
    }

    if (m_pDecoder->copyLatestFrame(m_uiFrame, m_lastRenderedFrame)) {
        update();
    }
}

void VideoWidget::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);
    QPainter painter(this);

    // Always fill black first so lowering fader opacity fades cleanly to black
    painter.fillRect(rect(), Qt::black);

    if (m_uiFrame.isNull() || !m_pDecoder) {
        return;
    }

    double opacity = m_pDecoder->getCalculatedOpacity();
    if (opacity <= 0.001) {
        return;
    }

    QSize frameSize = m_uiFrame.size();
    QRect targetRect = rect();
    QSize scaledSize = frameSize.scaled(targetRect.size(), Qt::KeepAspectRatio);

    int x = (targetRect.width() - scaledSize.width()) / 2;
    int y = (targetRect.height() - scaledSize.height()) / 2;
    QRect centeredLayoutRect(x, y, scaledSize.width(), scaledSize.height());

    painter.setOpacity(opacity);
    painter.drawImage(centeredLayoutRect, m_uiFrame);
}

void VideoWidget::slotLoadTrack(TrackPointer pTrack) {
    m_uiFrame = QImage();
    m_lastRenderedFrame = 0;
    update();

    if (m_pDecoder && pTrack) {
        m_pDecoder->openFile(pTrack->getLocation());
    }
}

#include "videowidget.moc"
