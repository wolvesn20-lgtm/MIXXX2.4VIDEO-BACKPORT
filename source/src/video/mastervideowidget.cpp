#include "video/mastervideowidget.h"
#include "video/videowidget.h"

#include <QAction>
#include <QCloseEvent>
#include <QMenu>
#include <QRandomGenerator>
#include <QTimer>
#include <QtGlobal>

ProjectorWindow::ProjectorWindow(MasterVideoWidget* pMaster, QWidget* parent)
        : QWidget(parent, Qt::Window),
          m_pMaster(pMaster) {
    setWindowTitle(QStringLiteral("Phoenix Master Video Output"));
    setAttribute(Qt::WA_OpaquePaintEvent);
    resize(1280, 720);
}

void ProjectorWindow::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);
    QPainter painter(this);
    if (m_pMaster) {
        m_pMaster->renderComposite(painter, rect(), true);
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
    m_pPlay1 = new ControlProxy(ConfigKey("[Channel1]", "play"), this);
    m_pPlay2 = new ControlProxy(ConfigKey("[Channel2]", "play"), this);

    m_pTransitionControl = new ControlObject(ConfigKey("[Master]", "video_transition"));
    m_pTransitionControl->set(0.0);

    m_pUpfaderPreviewControl = new ControlObject(ConfigKey("[Master]", "video_upfader_preview"));
    m_pUpfaderPreviewControl->set(1.0);

    m_pCueTransitionControl = new ControlObject(ConfigKey("[Master]", "video_transition_cue"));
    m_pCueTransitionControl->set(0.0);

    m_pAutoPlayControl = new ControlObject(ConfigKey("[Master]", "video_auto_play"));
    m_pAutoPlayControl->set(1.0);

    m_pRandomTransitionControl = new ControlObject(ConfigKey("[Master]", "video_random_transition"));
    m_pRandomTransitionControl->set(0.0);

    const QString btnStyle = QStringLiteral(
            "QPushButton { background-color: rgba(20, 20, 24, 190); color: #E0E0E0; "
            "border: 1px solid #555; border-radius: 3px; font-size: 10px; font-weight: bold; padding: 2px 6px; }"
            "QPushButton:checked { background-color: #D9534F; color: white; border-color: #FF7675; }"
            "QPushButton:hover { border-color: #FFA502; }");

    m_pCueBtn = new QPushButton(QStringLiteral("CUE"), this);
    m_pCueBtn->setCheckable(true);
    m_pCueBtn->setStyleSheet(btnStyle);
    m_pCueBtn->setToolTip(QStringLiteral("Cue Transition: Preview transition in Master without sending to Projector"));
    connect(m_pCueBtn, &QPushButton::toggled, this, [this](bool checked) {
        m_cueTransition = checked;
        if (m_pCueTransitionControl) {
            m_pCueTransitionControl->set(checked ? 1.0 : 0.0);
        }
        update();
    });

    m_pAutoTransBtn = new QPushButton(QStringLiteral("AUTO FADE"), this);
    m_pAutoTransBtn->setStyleSheet(btnStyle);
    m_pAutoTransBtn->setToolTip(QStringLiteral("Trigger smooth auto-transition between Deck 1 and Deck 2"));
    connect(m_pAutoTransBtn, &QPushButton::clicked, this, &MasterVideoWidget::triggerAutoTransition);

    m_pAutoPlayBtn = new QPushButton(QStringLiteral("AUTO PLAY"), this);
    m_pAutoPlayBtn->setCheckable(true);
    m_pAutoPlayBtn->setChecked(true);
    m_pAutoPlayBtn->setStyleSheet(btnStyle);
    m_pAutoPlayBtn->setToolTip(QStringLiteral("Auto-Play target deck video when transitioning"));
    connect(m_pAutoPlayBtn, &QPushButton::toggled, this, [this](bool checked) {
        m_autoPlayEnabled = checked;
        if (m_pAutoPlayControl) {
            m_pAutoPlayControl->set(checked ? 1.0 : 0.0);
        }
    });

    updateOverlayLayout();

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
        if (m_pCueTransitionControl) {
            bool wantCue = m_pCueTransitionControl->get() > 0.5;
            if (wantCue != m_cueTransition) {
                m_cueTransition = wantCue;
                if (m_pCueBtn) {
                    m_pCueBtn->setChecked(wantCue);
                }
            }
        }
        if (m_pAutoPlayControl) {
            bool wantAutoPlay = m_pAutoPlayControl->get() > 0.5;
            if (wantAutoPlay != m_autoPlayEnabled) {
                m_autoPlayEnabled = wantAutoPlay;
                if (m_pAutoPlayBtn) {
                    m_pAutoPlayBtn->setChecked(wantAutoPlay);
                }
            }
        }
        if (m_pRandomTransitionControl) {
            m_randomTransition = m_pRandomTransitionControl->get() > 0.5;
        }

        if (m_autoFadeActive && m_pCrossfader) {
            m_autoFadeProgress += 0.025; // ~1.3s smooth transition at 30fps
            if (m_autoFadeProgress >= 1.0) {
                m_autoFadeProgress = 1.0;
                m_autoFadeActive = false;
            }
            double nextXfader = m_autoFadeStart + (m_autoFadeTarget - m_autoFadeStart) * m_autoFadeProgress;
            m_pCrossfader->set(nextXfader);
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
    delete m_pCueTransitionControl;
    delete m_pAutoPlayControl;
    delete m_pRandomTransitionControl;
}

void MasterVideoWidget::updateOverlayLayout() {
    const int margin = 6;
    const int btnH = 20;
    int y = height() - btnH - margin;
    if (y < margin) {
        y = margin;
    }
    if (m_pCueBtn) {
        m_pCueBtn->setGeometry(margin, y, 42, btnH);
    }
    if (m_pAutoTransBtn) {
        m_pAutoTransBtn->setGeometry(margin + 46, y, 74, btnH);
    }
    if (m_pAutoPlayBtn) {
        m_pAutoPlayBtn->setGeometry(width() - 74 - margin, y, 74, btnH);
    }
}

void MasterVideoWidget::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    updateOverlayLayout();
}

void MasterVideoWidget::triggerAutoTransition() {
    if (!m_pCrossfader) {
        return;
    }
    if (m_randomTransition) {
        int randMode = QRandomGenerator::global()->bounded(5);
        setTransitionMode(static_cast<TransitionMode>(randMode));
    }
    double current = m_pCrossfader->get();
    m_autoFadeStart = current;
    m_autoFadeTarget = (current <= 0.0) ? 1.0 : -1.0;
    m_autoFadeProgress = 0.0;
    m_autoFadeActive = true;

    if (m_autoPlayEnabled) {
        if (m_autoFadeTarget > 0.0 && m_pPlay2 && m_pPlay2->get() < 0.5) {
            m_pPlay2->set(1.0);
        } else if (m_autoFadeTarget < 0.0 && m_pPlay1 && m_pPlay1->get() < 0.5) {
            m_pPlay1->set(1.0);
        }
    }
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

    QAction* cueAct = menu.addAction(QStringLiteral("Cue Transition (Preview in Master Only)"));
    cueAct->setCheckable(true);
    cueAct->setChecked(m_cueTransition);
    connect(cueAct, &QAction::triggered, this, [this](bool checked) {
        m_cueTransition = checked;
        if (m_pCueBtn) {
            m_pCueBtn->setChecked(checked);
        }
        if (m_pCueTransitionControl) {
            m_pCueTransitionControl->set(checked ? 1.0 : 0.0);
        }
        update();
    });

    QAction* autoPlayAct = menu.addAction(QStringLiteral("Auto-Play Deck on Transition"));
    autoPlayAct->setCheckable(true);
    autoPlayAct->setChecked(m_autoPlayEnabled);
    connect(autoPlayAct, &QAction::triggered, this, [this](bool checked) {
        m_autoPlayEnabled = checked;
        if (m_pAutoPlayBtn) {
            m_pAutoPlayBtn->setChecked(checked);
        }
