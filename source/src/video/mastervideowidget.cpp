#include "video/mastervideowidget.h"
#include "video/videowidget.h"

#include <QAction>
#include <QCloseEvent>
#include <QMenu>
#include <QTimer>
#include <QtGlobal>

ProjectorWindow::ProjectorWindow(MasterVideoWidget* pMaster, QWidget* parent)
        : QWidget(parent, Qt::Window),
          m_pMaster(pMaster) {
    setWindowTitle(QStringLiteral("Mixxx Master Video Output"));
    setAttribute(Qt::WA_OpaquePaintEvent);
    resize(1280, 720);
}

void ProjectorWindow::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);
    QPainter painter(this);
    if (m_pMaster) {
        m_pMaster->renderComposite(painter, rect());
    } else {
        painter.fillRect(rect(), Qt::black);
    }
}

void ProjectorWindow::mouseDoubleClickEvent(QMouseEvent* event) {
    Q_UNUSED(event);
    if (isFullScreen()) {
        showNormal();
    } else {
        showFullScreen();
    }
}

void ProjectorWindow::closeEvent(QCloseEvent* event) {
    emit windowClosed();
    QWidget::closeEvent(event);
}

MasterVideoWidget::MasterVideoWidget(QWidget* parent)
        : QWidget(parent) {
    setAttribute(Qt::WA_OpaquePaintEvent);
    setToolTip(QStringLiteral("Double-click: Pop out Projector | Right-click: Transitions & Preview Options"));

    m_pCrossfader = new ControlProxy(ConfigKey("[Master]", "crossfader"), this);
    m_pVol1 = new ControlProxy(ConfigKey("[Channel1]", "volume"), this);
    m_pVol2 = new ControlProxy(ConfigKey("[Channel2]", "volume"), this);

    m_pTransitionControl = new ControlObject(ConfigKey("[Master]", "video_transition"));
    m_pTransitionControl->set(0.0);

    m_pUpfaderPreviewControl = new ControlObject(ConfigKey("[Master]", "video_upfader_preview"));
    m_pUpfaderPreviewControl->set(1.0);

    QTimer* pRefreshTimer = new QTimer(this);
    connect(pRefreshTimer, &QTimer::timeout, this, [this]() {
        if (m_pTransitionControl) {
            int modeVal = qBound(0, static_cast<int>(m_pTransitionControl->get()), 4);
            m_transition = static_cast<TransitionMode>(modeVal);
        }
        if (m_pUpfaderPreviewControl) {
            bool wantPreview = m_pUpfaderPreviewControl->get() > 0.5;
            if (wantPreview != VideoWidget::isUpfaderPreviewEnabled()) {
                VideoWidget::setUpfaderPreviewEnabled(wantPreview);
            }
        }
        update();
        if (m_pProjectorWindow && m_pProjectorWindow->isVisible()) {
            m_pProjectorWindow->update();
        }
    });
    pRefreshTimer->start(33);
}

MasterVideoWidget::~MasterVideoWidget() {
    if (m_pProjectorWindow) {
        m_pProjectorWindow->close();
        delete m_pProjectorWindow;
    }
    delete m_pTransitionControl;
    delete m_pUpfaderPreviewControl;
}

void MasterVideoWidget::setTransitionMode(TransitionMode mode) {
    m_transition = mode;
    if (m_pTransitionControl) {
        m_pTransitionControl->set(static_cast<double>(mode));
    }
    update();
}

void MasterVideoWidget::toggleProjectorWindow() {
    if (m_pProjectorWindow && m_pProjectorWindow->isVisible()) {
        m_pProjectorWindow->close();
        return;
    }

    if (!m_pProjectorWindow) {
        m_pProjectorWindow = new ProjectorWindow(this);
        connect(m_pProjectorWindow, &ProjectorWindow::windowClosed, this, [this]() {
            m_pProjectorWindow->deleteLater();
            m_pProjectorWindow = nullptr;
        });
    }

    m_pProjectorWindow->show();
    m_pProjectorWindow->raise();
    m_pProjectorWindow->activateWindow();
}

void MasterVideoWidget::mouseDoubleClickEvent(QMouseEvent* event) {
    Q_UNUSED(event);
    toggleProjectorWindow();
}

void MasterVideoWidget::contextMenuEvent(QContextMenuEvent* event) {
    QMenu menu(this);

    QAction* popOutAct = menu.addAction(
            (m_pProjectorWindow && m_pProjectorWindow->isVisible())
                    ? QStringLiteral("Close Projector Window")
                    : QStringLiteral("Pop Out Projector Window"));
    connect(popOutAct, &QAction::triggered, this, &MasterVideoWidget::toggleProjectorWindow);

    QAction* faderPreviewAct = menu.addAction(QStringLiteral("Deck Preview Follows Upfaders"));
    faderPreviewAct->setCheckable(true);
    faderPreviewAct->setChecked(VideoWidget::isUpfaderPreviewEnabled());
    connect(faderPreviewAct, &QAction::triggered, this, [this](bool checked) {
        VideoWidget::setUpfaderPreviewEnabled(checked);
        if (m_pUpfaderPreviewControl) {
            m_pUpfaderPreviewControl->set(checked ? 1.0 : 0.0);
        }
    });

    menu.addSeparator();

    struct ModeItem {
        QString label;
        TransitionMode mode;
    };
    const QList<ModeItem> modes = {
            {QStringLiteral("Transition: Mix (Dissolve)"), TransitionMode::Mix},
            {QStringLiteral("Transition: Wipe Horizontal"), TransitionMode::WipeHorizontal},
            {QStringLiteral("Transition: Wipe Vertical"), TransitionMode::WipeVertical},
            {QStringLiteral("Transition: Cut (Hard Switch)"), TransitionMode::Cut},
            {QStringLiteral("Transition: Zoom Push"), TransitionMode::Zoom},
    };

    for (const auto& item : modes) {
        QAction* act = menu.addAction(item.label);
        act->setCheckable(true);
        act->setChecked(m_transition == item.mode);
        connect(act, &QAction::triggered, this, [this, item]() {
            setTransitionMode(item.mode);
        });
    }

    menu.exec(event->globalPos());
}

void MasterVideoWidget::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);
    QPainter painter(this);
    renderComposite(painter, rect());
}

void MasterVideoWidget::drawCentered(
        QPainter& painter,
        const QRect& targetRect,
        const QImage& img,
        double opacity) {
    if (img.isNull() || opacity <= 0.001) {
        return;
    }

    QSize scaledSize = img.size().scaled(targetRect.size(), Qt::KeepAspectRatio);
    int x = targetRect.x() + (targetRect.width() - scaledSize.width()) / 2;
    int y = targetRect.y() + (targetRect.height() - scaledSize.height()) / 2;

    painter.save();
    painter.setOpacity(qBound(0.0, opacity, 1.0));
    painter.drawImage(QRect(x, y, scaledSize.width(), scaledSize.height()), img);
    painter.restore();
}

void MasterVideoWidget::renderComposite(QPainter& painter, const QRect& targetRect) {
    painter.fillRect(targetRect, Qt::black);

    VideoWidget* deck1 = VideoWidget::getDeckWidget(QStringLiteral("[Channel1]"));
    VideoWidget* deck2 = VideoWidget::getDeckWidget(QStringLiteral("[Channel2]"));

    QImage img1 = deck1 ? deck1->currentFrame() : QImage();
    QImage img2 = deck2 ? deck2->currentFrame() : QImage();

    double xfader = m_pCrossfader ? qBound(-1.0, m_pCrossfader->get(), 1.0) : 0.0;
    double mix = (xfader + 1.0) * 0.5;
    double vol1 = m_pVol1 ? qBound(0.0, m_pVol1->get(), 1.0) : 1.0;
    double vol2 = m_pVol2 ? qBound(0.0, m_pVol2->get(), 1.0) : 1.0;

    switch (m_transition) {
        case TransitionMode::Mix: {
            double op1 = vol1 * (xfader <= 0.0 ? 1.0 : (1.0 - xfader));
            double op2 = vol2 * (xfader >= 0.0 ? 1.0 : (1.0 + xfader));
            drawCentered(painter, targetRect, img1, op1);
            drawCentered(painter, targetRect, img2, op2);
            break;
        }

        case TransitionMode::Cut: {
            if (xfader <= 0.0) {
                drawCentered(painter, targetRect, img1, vol1);
            } else {
                drawCentered(painter, targetRect, img2, vol2);
            }
            break;
        }

        case TransitionMode::WipeHorizontal: {
            int splitX = targetRect.x() + static_cast<int>(targetRect.width() * (1.0 - mix));
            if (splitX > targetRect.x()) {
                painter.save();
                painter.setClipRect(QRect(targetRect.x(), targetRect.y(), splitX - targetRect.x(), targetRect.height()));
                drawCentered(painter, targetRect, img1, vol1);
                painter.restore();
            }
            if (splitX < targetRect.right()) {
                painter.save();
                painter.setClipRect(QRect(splitX, targetRect.y(), targetRect.right() - splitX + 1, targetRect.height()));
                drawCentered(painter, targetRect, img2, vol2);
                painter.restore();
            }
            break;
        }

        case TransitionMode::WipeVertical: {
            int splitY = targetRect.y() + static_cast<int>(targetRect.height() * (1.0 - mix));
            if (splitY > targetRect.y()) {
                painter.save();
                painter.setClipRect(QRect(targetRect.x(), targetRect.y(), targetRect.width(), splitY - targetRect.y()));
                drawCentered(painter, targetRect, img1, vol1);
                painter.restore();
            }
            if (splitY < targetRect.bottom()) {
                painter.save();
                painter.setClipRect(QRect(targetRect.x(), splitY, targetRect.width(), targetRect.bottom() - splitY + 1));
                drawCentered(painter, targetRect, img2, vol2);
                painter.restore();
            }
            break;
        }

        case TransitionMode::Zoom: {
            double scale1 = 1.0 + (mix * 0.5);
            QRect rect1(
                    targetRect.center().x() - static_cast<int>(targetRect.width() * scale1 * 0.5),
                    targetRect.center().y() - static_cast<int>(targetRect.height() * scale1 * 0.5),
                    static_cast<int>(targetRect.width() * scale1),
                    static_cast<int>(targetRect.height() * scale1));
            drawCentered(painter, rect1, img1, vol1 * (1.0 - mix));

            double scale2 = 0.5 + (mix * 0.5);
            QRect rect2(
                    targetRect.center().x() - static_cast<int>(targetRect.width() * scale2 * 0.5),
                    targetRect.center().y() - static_cast<int>(targetRect.height() * scale2 * 0.5),
                    static_cast<int>(targetRect.width() * scale2),
                    static_cast<int>(targetRect.height() * scale2));
            drawCentered(painter, rect2, img2, vol2 * mix);
            break;
        }
    }
}
