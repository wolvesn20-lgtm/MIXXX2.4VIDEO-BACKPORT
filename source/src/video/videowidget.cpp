#include "video/videowidget.h"
#include "control/controlproxy.h"
#include "video/mastervideowidget.h"

#include <QDir>
#include <QFileInfo>
#include <QMimeData>
#include <QPainter>
#include <QTimer>
#include <QUrl>

QMap<QString, VideoWidget*> VideoWidget::s_deckWidgets;
MasterVideoWidget* VideoWidget::s_pMasterWidget = nullptr;
bool VideoWidget::s_upfaderPreview = true;

VideoWidget::VideoWidget(const QString& group, QWidget* parent)
        : QWidget(parent), m_group(group) {
    setAttribute(Qt::WA_OpaquePaintEvent);
    setAcceptDrops(true);
    setToolTip(QStringLiteral("Drag & drop a video or track here to load into ") + m_group);
    s_deckWidgets.insert(m_group, this);

    QTimer* pFaderTimer = new QTimer(this);
    connect(pFaderTimer, &QTimer::timeout, this, QOverload<>::of(&VideoWidget::update));
    pFaderTimer->start(33);

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

bool VideoWidget::isUpfaderPreviewEnabled() {
    return s_upfaderPreview;
}

void VideoWidget::setUpfaderPreviewEnabled(bool enabled) {
    s_upfaderPreview = enabled;
    for (auto* w : s_deckWidgets) {
        if (w) {
            w->update();
        }
    }
}

void VideoWidget::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    syncMasterPreviewGeometry();
}

void VideoWidget::moveEvent(QMoveEvent* event) {
    QWidget::moveEvent(event);
    syncMasterPreviewGeometry();
}

void VideoWidget::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData() && (event->mimeData()->hasUrls() || event->mimeData()->hasText())) {
        event->acceptProposedAction();
    }
}

void VideoWidget::dropEvent(QDropEvent* event) {
    if (!event->mimeData()) {
        return;
    }

    QString filePath;
    if (event->mimeData()->hasUrls()) {
        const QList<QUrl> urls = event->mimeData()->urls();
        if (!urls.isEmpty()) {
            filePath = urls.first().toLocalFile();
        }
    } else if (event->mimeData()->hasText()) {
        QString text = event->mimeData()->text().trimmed();
        QUrl url(text);
        filePath = url.isLocalFile() ? url.toLocalFile() : text;
    }

    if (!filePath.isEmpty() && QFileInfo::exists(filePath)) {
        m_uiFrame = QImage();
        m_lastRenderedFrame = 0;
        if (m_pDecoder) {
            m_pDecoder->openFile(filePath);
        }
        ControlProxy loadProxy(ConfigKey(m_group, "LoadSelectedTrack"));
        if (loadProxy.valid()) {
            loadProxy.set(1.0);
        }
        update();
        event->acceptProposedAction();
    }
}

void VideoWidget::syncMasterPreviewGeometry() {
    VideoWidget* deck1 = s_deckWidgets.value(QStringLiteral("[Channel1]"), nullptr);
    VideoWidget* deck2 = s_deckWidgets.value(QStringLiteral("[Channel2]"), nullptr);
    if (!deck1 || !deck2 || !deck1->window()) {
        return;
    }

    QWidget* topWin = deck1->window();

    // Rebrand main window title from Mixxx to Phoenix
    QString currentTitle = topWin->windowTitle();
    if (currentTitle.contains(QStringLiteral("Mixxx"), Qt::CaseInsensitive)) {
        currentTitle.replace(QStringLiteral("Mixxx"), QStringLiteral("Phoenix"), Qt::CaseInsensitive);
        topWin->setWindowTitle(currentTitle);
    } else if (currentTitle.isEmpty()) {
        topWin->setWindowTitle(QStringLiteral("Phoenix"));
    }

    if (!s_pMasterWidget) {
        s_pMasterWidget = new MasterVideoWidget(topWin);
    } else if (s_pMasterWidget->parentWidget() != topWin) {
        s_pMasterWidget->setParent(topWin);
    }

    int height = qMax(deck1->height(), deck2->height());
    if (height < 90) {
        height = 90;
    }
    int widescreenWidth = (height * 16) / 9;

    // Push Deck 1 outward to the left edge of its container and expand to 16:9
    if (QWidget* p1 = deck1->parentWidget()) {
        int newW1 = qMin(widescreenWidth, qMax(deck1->width(), p1->width() - 8));
        int newX1 = 4;
        if (deck1->x() != newX1 || deck1->width() != newW1) {
            deck1->setMinimumWidth(newW1);
            deck1->setGeometry(newX1, deck1->y(), newW1, deck1->height());
        }
    }

    // Push Deck 2 outward to the right edge of its container and expand to 16:9
    if (QWidget* p2 = deck2->parentWidget()) {
        int newW2 = qMin(widescreenWidth, qMax(deck2->width(), p2->width() - 8));
        int newX2 = qMax(4, p2->width() - newW2 - 4);
        if (deck2->x() != newX2 || deck2->width() != newW2) {
            deck2->setMinimumWidth(newW2);
            deck2->setGeometry(newX2, deck2->y(), newW2, deck2->height());
        }
    }

    // Recalculate global coordinates after pushing Deck 1 & Deck 2 outward
    QPoint d1TopLeft = deck1->mapTo(topWin, QPoint(0, 0));
    QPoint d2TopLeft = deck2->mapTo(topWin, QPoint(0, 0));

    int topY = qMin(d1TopLeft.y(), d2TopLeft.y());
    int gapLeft = d1TopLeft.x() + deck1->width() + 8;
    int gapRight = d2TopLeft.x() - 8;
    int gapWidth = gapRight - gapLeft;

    int finalWidth = (gapWidth >= 140) ? qMin(gapWidth, widescreenWidth) : widescreenWidth;
    int centerX = (d1TopLeft.x() + deck1->width() + d2TopLeft.x()) / 2;
    int finalLeft = centerX - (finalWidth / 2);

    s_pMasterWidget->setGeometry(finalLeft, topY, finalWidth, height);
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
    painter.fillRect(rect(), Qt::black);

    if (m_uiFrame.isNull() || !m_pDecoder) {
        painter.setPen(QColor(130, 130, 140));
        QFont f = painter.font();
        f.setPointSize(9);
        f.setBold(true);
        painter.setFont(f);
        QString deckLabel = (m_group == QStringLiteral("[Channel1]"))
                ? QStringLiteral("PHOENIX DECK 1\n(Drop Video)")
                : QStringLiteral("PHOENIX DECK 2\n(Drop Video)");
        painter.drawText(rect(), Qt::AlignCenter, deckLabel);
        return;
    }

    double opacity = s_upfaderPreview ? m_pDecoder->getCalculatedOpacity() : 1.0;
    if (opacity <= 0.001) {
        return;
    }

    QSize frameSize = m_uiFrame.size();
    QRect targetRect = rect();
    QSize scaledSize = frameSize.scaled(targetRect.size(), Qt::KeepAspectRatioByExpanding);

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
