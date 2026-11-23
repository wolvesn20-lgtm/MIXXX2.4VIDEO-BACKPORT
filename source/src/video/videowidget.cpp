#include "video/videowidget.h"
#include "video/mastervideowidget.h"

#include <QPainter>
#include <QTimer>

QMap<QString, VideoWidget*> VideoWidget::s_deckWidgets;
MasterVideoWidget* VideoWidget::s_pMasterWidget = nullptr;

VideoWidget::VideoWidget(const QString& group, QWidget* parent)
        : QWidget(parent), m_group(group) {
    setAttribute(Qt::WA_OpaquePaintEvent);
    s_deckWidgets.insert(m_group, this);

    // Lightweight timer so fader/crossfader opacity changes stay smooth even when paused
    QTimer* pFaderTimer = new QTimer(this);
    connect(pFaderTimer, &QTimer::timeout, this, QOverload<>::of(&VideoWidget::update));
    pFaderTimer->start(33); // ~30 Hz UI opacity refresh

    QTimer::singleShot(0, this, &VideoWidget::syncMasterPreviewGeometry);
}

VideoWidget::~VideoWidget() {
    if (s_deckWidgets.value(m_group) == this) {
        s_deckWidgets.remove(m_group);
    }
    if (s_deckWidgets.isEmpty() && s_pMasterWidget) {
        s_pMasterWidget->deleteLater();
        s_pMasterWidget = nullptr;
    }
}

VideoWidget* VideoWidget::getDeckWidget(const QString& group) {
    return s_deckWidgets.value(group, nullptr);
}

void VideoWidget::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    syncMasterPreviewGeometry();
}

void VideoWidget::moveEvent(QMoveEvent* event) {
    QWidget::moveEvent(event);
    syncMasterPreviewGeometry();
}

void VideoWidget::syncMasterPreviewGeometry() {
    VideoWidget* deck1 = s_deckWidgets.value(QStringLiteral("[Channel1]"), nullptr);
    VideoWidget* deck2 = s_deckWidgets.value(QStringLiteral("[Channel2]"), nullptr);
    if (!deck1 || !deck2 || !deck1->window()) {
        return;
    }

    QWidget* topWin = deck1->window();
    if (!s_pMasterWidget) {
        s_pMasterWidget = new MasterVideoWidget(topWin);
    } else if (s_pMasterWidget->parentWidget() != topWin) {
        s_pMasterWidget->setParent(topWin);
    }

    // Map Deck 1 and Deck 2 positions into the main window's coordinate space
    QPoint d1TopLeft = deck1->mapTo(topWin, QPoint(0, 0));
    QPoint d2TopLeft = deck2->mapTo(topWin, QPoint(0, 0));

    int gapLeft = d1TopLeft.x() + deck1->width() + 6;
    int gapRight = d2TopLeft.x() - 6;
    int gapWidth = gapRight - gapLeft;

    // Fallback width if decks are close together
    if (gapWidth < 120) {
        int centerX = (d1TopLeft.x() + deck1->width() + d2TopLeft.x()) / 2;
        gapWidth = qMax(140, deck1->width());
        gapLeft = centerX - (gapWidth / 2);
    }

    int topY = qMin(d1TopLeft.y(), d2TopLeft.y());
    int height = qMax(deck1->height(), deck2->height());

    s_pMasterWidget->setGeometry(gapLeft, topY, gapWidth, height);
    s_pMasterWidget->show();
    s_pMasterWidget->raise();
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
