#include "video/mastervideowidget.h"
#include "video/videowidget.h"

#include <QAction>
#include <QCloseEvent>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLinearGradient>
#include <QMenu>
#include <QRandomGenerator>
#include <QTimer>
#include <QVBoxLayout>
#include <QtGlobal>
#include <QtMath>

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
    if (!m_isCustomFullScreen) {
        m_savedGeometry = geometry();
        m_isCustomFullScreen = true;

        QScreen* pScreen = windowHandle() ? windowHandle()->screen() : QGuiApplication::primaryScreen();
        QRect screenGeom = pScreen ? pScreen->geometry() : QRect(0, 0, 1280, 720);

        setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
        setGeometry(screenGeom);
        show();
        raise();
        activateWindow();
    } else {
        m_isCustomFullScreen = false;

        setWindowFlags(Qt::Window);
        if (m_savedGeometry.isValid()) {
            setGeometry(m_savedGeometry);
        } else {
            resize(1280, 720);
        }
        show();
        raise();
        activateWindow();
    }
}

void ProjectorWindow::closeEvent(QCloseEvent* event) {
    emit windowClosed();
    QWidget::closeEvent(event);
}

MasterVideoWidget::MasterVideoWidget(QWidget* parent)
        : QWidget(parent) {
    setAttribute(Qt::WA_OpaquePaintEvent);
    setToolTip(QStringLiteral("Double-click: Pop out Phoenix Projector | Right-click: Edit Texts & Options"));

    m_pCrossfader = new ControlProxy(ConfigKey("[Master]", "crossfader"), this);
    m_pVol1 = new ControlProxy(ConfigKey("[Channel1]", "volume"), this);
    m_pVol2 = new ControlProxy(ConfigKey("[Channel2]", "volume"), this);
    m_pPlay1 = new ControlProxy(ConfigKey("[Channel1]", "play"), this);
    m_pPlay2 = new ControlProxy(ConfigKey("[Channel2]", "play"), this);
    m_pBeatActive1 = new ControlProxy(ConfigKey("[Channel1]", "beat_active"), this);
    m_pBeatActive2 = new ControlProxy(ConfigKey("[Channel2]", "beat_active"), this);

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

    m_pBeatSyncControl = new ControlObject(ConfigKey("[Master]", "video_beat_sync"));
    m_pBeatSyncControl->set(0.0);

    m_pCameraSourceControl = new ControlObject(ConfigKey("[Master]", "video_camera_source"));
    m_pCameraSourceControl->set(0.0);

    m_pLowerThirdControl = new ControlObject(ConfigKey("[Master]", "video_lower_third"));
    m_pLowerThirdControl->set(0.0);

    m_pOverlayControl = new ControlObject(ConfigKey("[Master]", "video_overlay"));
    m_pOverlayControl->set(0.0);

    m_pRgbSplashControl = new ControlObject(ConfigKey("[Master]", "video_rgb_splash"));
    m_pRgbSplashControl->set(0.0);

    buildQuickToolbar();

    QTimer* pRefreshTimer = new QTimer(this);
    connect(pRefreshTimer, &QTimer::timeout, this, [this]() {
        m_rgbPhase = (m_rgbPhase + 6) % 360;

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
            m_cueTransition = m_pCueTransitionControl->get() > 0.5;
        }
        if (m_pAutoPlayControl) {
            m_autoPlayEnabled = m_pAutoPlayControl->get() > 0.5;
        }
        if (m_pRandomTransitionControl) {
            m_randomTransition = m_pRandomTransitionControl->get() > 0.5;
        }
        if (m_pBeatSyncControl) {
            m_beatSyncEnabled = m_pBeatSyncControl->get() > 0.5;
        }
        if (m_pCameraSourceControl) {
            m_cameraSource = qBound(0, static_cast<int>(m_pCameraSourceControl->get()), 3);
        }
        if (m_pLowerThirdControl) {
            m_lowerThirdEnabled = m_pLowerThirdControl->get() > 0.5;
        }
        if (m_pOverlayControl) {
            m_overlayEnabled = m_pOverlayControl->get() > 0.5;
        }
        if (m_pRgbSplashControl) {
            m_rgbSplashEnabled = m_pRgbSplashControl->get() > 0.5;
        }

        if (m_beatSyncEnabled && m_randomTransition) {
            bool beatHit = (m_pBeatActive1 && m_pBeatActive1->get() > 0.5) ||
                           (m_pBeatActive2 && m_pBeatActive2->get() > 0.5);
            static bool prevBeat = false;
            if (beatHit && !prevBeat) {
                int nextMode = QRandomGenerator::global()->bounded(5);
                setTransitionMode(static_cast<TransitionMode>(nextMode));
            }
            prevBeat = beatHit;
        }

        if (m_autoFadeActive && m_pCrossfader) {
            m_autoFadeProgress += 0.03;
            if (m_autoFadeProgress >= 1.0) {
                m_autoFadeProgress = 1.0;
                m_autoFadeActive = false;
            }
            double nextXfader = m_autoFadeStart + (m_autoFadeTarget - m_autoFadeStart) * m_autoFadeProgress;
            m_pCrossfader->set(nextXfader);
        }

        syncToolbarStates();
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
    delete m_pBeatSyncControl;
    delete m_pCameraSourceControl;
    delete m_pLowerThirdControl;
    delete m_pOverlayControl;
    delete m_pRgbSplashControl;
}

void MasterVideoWidget::buildQuickToolbar() {
    m_pToolbarContainer = new QWidget(this);
    m_pToolbarContainer->setStyleSheet(QStringLiteral(
            "QWidget { background-color: rgba(15, 15, 20, 195); border-radius: 4px; }"
            "QPushButton, QComboBox { background-color: #121218; color: #EAEAEF; border: 1px solid #3C3C48; "
            "border-radius: 3px; font-size: 8px; font-weight: bold; padding: 1px 5px; min-height: 15px; }"
            "QPushButton:checked { background-color: #E85D04; color: #FFFFFF; border-color: #FF9E00; }"
            "QPushButton:hover, QComboBox:hover { border-color: #FF9E00; }"
            "QComboBox::drop-down { border: none; width: 12px; }"
            "QComboBox QAbstractItemView { background-color: #16161D; color: #EAEAEF; selection-background-color: #E85D04; }"));

    QVBoxLayout* mainLayout = new QVBoxLayout(m_pToolbarContainer);
    mainLayout->setContentsMargins(4, 3, 4, 3);
    mainLayout->setSpacing(3);

    QHBoxLayout* row1 = new QHBoxLayout();
    row1->setSpacing(4);

    m_pTransitionDropdown = new QComboBox(m_pToolbarContainer);
    m_pTransitionDropdown->setFocusPolicy(Qt::NoFocus);
    m_pTransitionDropdown->addItem(QStringLiteral("Trans: Mix ▾"), static_cast<int>(TransitionMode::Mix));
    m_pTransitionDropdown->addItem(QStringLiteral("Trans: Wipe H ▾"), static_cast<int>(TransitionMode::WipeHorizontal));
    m_pTransitionDropdown->addItem(QStringLiteral("Trans: Wipe V ▾"), static_cast<int>(TransitionMode::WipeVertical));
    m_pTransitionDropdown->addItem(QStringLiteral("Trans: Cut ▾"), static_cast<int>(TransitionMode::Cut));
    m_pTransitionDropdown->addItem(QStringLiteral("Trans: Zoom ▾"), static_cast<int>(TransitionMode::Zoom));
    m_pTransitionDropdown->addItem(QStringLiteral("Option: Cue Preview"), 100);
    m_pTransitionDropdown->addItem(QStringLiteral("Option: Auto Play"), 101);
    m_pTransitionDropdown->addItem(QStringLiteral("Option: Random Trans"), 102);
    m_pTransitionDropdown->addItem(QStringLiteral("Option: Beat Sync"), 103);

    m_pBtnFadeNow = new QPushButton(QStringLiteral("FADE"), m_pToolbarContainer);
    m_pBtnFadeNow->setFocusPolicy(Qt::NoFocus);
    m_pBtnFadeNow->setToolTip(QStringLiteral("Trigger smooth auto-fade between Deck 1 & Deck 2"));

    m_pBtnProjector = new QPushButton(QStringLiteral("PHOENIX OUT"), m_pToolbarContainer);
    m_pBtnProjector->setCheckable(true);
    m_pBtnProjector->setFocusPolicy(Qt::NoFocus);

    row1->addWidget(m_pTransitionDropdown, 3);
    row1->addWidget(m_pBtnFadeNow, 1);
    row1->addWidget(m_pBtnProjector, 2);

    m_pBtnCamDropdown = new QPushButton(QStringLiteral("CAM: OFF ▾"), m_pToolbarContainer);
    m_pBtnCamDropdown->setCheckable(true);
    m_pBtnCamDropdown->setFocusPolicy(Qt::NoFocus);

    m_pBtnLowerThird = new QPushButton(QStringLiteral("LOWER 3RD"), m_pToolbarContainer);
    m_pBtnLowerThird->setCheckable(true);
    m_pBtnLowerThird->setFocusPolicy(Qt::NoFocus);
    m_pBtnLowerThird->setToolTip(QStringLiteral("Toggle Lower Third banner (Right-click to edit text)"));

    m_pBtnOverlay = new QPushButton(QStringLiteral("OVERLAY"), m_pToolbarContainer);
    m_pBtnOverlay->setCheckable(true);
    m_pBtnOverlay->setFocusPolicy(Qt::NoFocus);
    m_pBtnOverlay->setToolTip(QStringLiteral("Toggle Visual Overlay layer"));

    m_pBtnRgbSplash = new QPushButton(QStringLiteral("RGB SPLASH"), m_pToolbarContainer);
    m_pBtnRgbSplash->setCheckable(true);
    m_pTransitionDropdown->addItem(QStringLiteral("Option: Auto Play"), 101);
    m_pTransitionDropdown->addItem(QStringLiteral("Option: Randomize"), 102);
    m_pTransitionDropdown->addItem(QStringLiteral("Option: Beat Sync"), 103);

    m_pBtnFadeNow = new QPushButton(QStringLiteral("FADE"), m_pToolbarContainer);
    m_pBtnFadeNow->setFocusPolicy(Qt::NoFocus);
    m_pBtnFadeNow->setToolTip(QStringLiteral("Trigger smooth auto-fade between Deck 1 & Deck 2"));

    m_pBtnProjector = new QPushButton(QStringLiteral("PHOENIX OUT"), m_pToolbarContainer);
    m_pBtnProjector->setCheckable(true);
    m_pBtnProjector->setFocusPolicy(Qt::NoFocus);

    row1->addWidget(m_pTransitionDropdown, 3);
    row1->addWidget(m_pBtnFadeNow, 1);
    row1->addWidget(m_pBtnProjector, 2);

    QHBoxLayout* row2 = new QHBoxLayout();
    row2->setSpacing(4);

    m_pBtnCamDropdown = new QPushButton(QStringLiteral("CAM: OFF ▾"), m_pToolbarContainer);
    m_pBtnCamDropdown->setCheckable(true);
    m_pBtnCamDropdown->setFocusPolicy(Qt::NoFocus);

    m_pBtnLowerThird = new QPushButton(QStringLiteral("LOWER 3RD"), m_pToolbarContainer);
    m_pBtnLowerThird->setCheckable(true);
    m_pBtnLowerThird->setFocusPolicy(Qt::NoFocus);
    m_pBtnLowerThird->setToolTip(QStringLiteral("Toggle Lower Third banner (Right-click to edit text)"));

    m_pBtnOverlay = new QPushButton(QStringLiteral("OVERLAY"), m_pToolbarContainer);
    m_pBtnOverlay->setCheckable(true);
    m_pBtnOverlay->setFocusPolicy(Qt::NoFocus);
    m_pBtnOverlay->setToolTip(QStringLiteral("Toggle Visual Overlay layer"));

    m_pBtnRgbSplash = new QPushButton(QStringLiteral("RGB SPLASH"), m_pToolbarContainer);
    m_pBtnRgbSplash->setCheckable(true);
    m_pBtnRgbSplash->setFocusPolicy(Qt::NoFocus);
    m_pBtnRgbSplash->setToolTip(QStringLiteral("Full-screen multi-color LED billboard text splash (Right-click to edit text)"));

    row2->addWidget(m_pBtnCamDropdown, 2);
    row2->addWidget(m_pBtnLowerThird, 2);
    row2->addWidget(m_pBtnOverlay, 2);
    row2->addWidget(m_pBtnRgbSplash, 2);

    mainLayout->addLayout(row1);
    mainLayout->addLayout(row2);

    connect(m_pTransitionDropdown, QOverload<int>::of(&QComboBox::activated), this, [this](int index) {
        int code = m_pTransitionDropdown->itemData(index).toInt();
        if (code >= 0 && code <= 4) {
            setTransitionMode(static_cast<TransitionMode>(code));
        } else if (code == 100) {
            m_cueTransition = !m_cueTransition;
            if (m_pCueTransitionControl) m_pCueTransitionControl->set(m_cueTransition ? 1.0 : 0.0);
        } else if (code == 101) {
            m_autoPlayEnabled = !m_autoPlayEnabled;
            if (m_pAutoPlayControl) m_pAutoPlayControl->set(m_autoPlayEnabled ? 1.0 : 0.0);
        } else if (code == 102) {
            m_randomTransition = !m_randomTransition;
            if (m_pRandomTransitionControl) m_pRandomTransitionControl->set(m_randomTransition ? 1.0 : 0.0);
        } else if (code == 103) {
            m_beatSyncEnabled = !m_beatSyncEnabled;
            if (m_pBeatSyncControl) m_pBeatSyncControl->set(m_beatSyncEnabled ? 1.0 : 0.0);
        }
        syncToolbarStates();
        update();
    });

    connect(m_pBtnFadeNow, &QPushButton::clicked, this, &MasterVideoWidget::triggerAutoTransition);
    connect(m_pBtnProjector, &QPushButton::clicked, this, &MasterVideoWidget::toggleProjectorWindow);

    connect(m_pBtnCamDropdown, &QPushButton::clicked, this, [this]() {
        QMenu camMenu(this);
        QAction* offAct = camMenu.addAction(QStringLiteral("Camera Off (Decks Only)"));
        QAction* cam1Act = camMenu.addAction(QStringLiteral("Camera 1 (Built-in / USB)"));
        QAction* cam2Act = camMenu.addAction(QStringLiteral("Camera 2 (External USB)"));
        QAction* wifiAct = camMenu.addAction(QStringLiteral("Camera 3 (Wi-Fi Hotspot Stream)"));
        offAct->setCheckable(true); offAct->setChecked(m_cameraSource == 0);
        cam1Act->setCheckable(true); cam1Act->setChecked(m_cameraSource == 1);
        cam2Act->setCheckable(true); cam2Act->setChecked(m_cameraSource == 2);
        wifiAct->setCheckable(true); wifiAct->setChecked(m_cameraSource == 3);

        QAction* chosen = camMenu.exec(m_pBtnCamDropdown->mapToGlobal(QPoint(0, m_pBtnCamDropdown->height())));
        if (chosen == offAct) m_cameraSource = 0;
        else if (chosen == cam1Act) m_cameraSource = 1;
        else if (chosen == cam2Act) m_cameraSource = 2;
        else if (chosen == wifiAct) m_cameraSource = 3;

        if (m_pCameraSourceControl) {
            m_pCameraSourceControl->set(static_cast<double>(m_cameraSource));
        }
        syncToolbarStates();
        update();
    });

    connect(m_pBtnLowerThird, &QPushButton::toggled, this, [this](bool checked) {
        m_lowerThirdEnabled = checked;
        if (m_pLowerThirdControl) m_pLowerThirdControl->set(checked ? 1.0 : 0.0);
        update();
    });

    connect(m_pBtnOverlay, &QPushButton::toggled, this, [this](bool checked) {
        m_overlayEnabled = checked;
        if (m_pOverlayControl) m_pOverlayControl->set(checked ? 1.0 : 0.0);
        update();
    });

    connect(m_pBtnRgbSplash, &QPushButton::toggled, this, [this](bool checked) {
        m_rgbSplashEnabled = checked;
        if (m_pRgbSplashControl) m_pRgbSplashControl->set(checked ? 1.0 : 0.0);
        update();
    });

    syncToolbarStates();
    updateToolbarGeometry();
}

void MasterVideoWidget::syncToolbarStates() {
    if (!m_pToolbarContainer) {
        return;
    }
    int modeIdx = static_cast<int>(m_transition);
    if (m_pTransitionDropdown && m_pTransitionDropdown->currentIndex() != modeIdx) {
        m_pTransitionDropdown->setCurrentIndex(modeIdx);
    }

    if (m_pBtnCamDropdown) {
        m_pBtnCamDropdown->setChecked(m_cameraSource > 0);
        if (m_cameraSource == 0) m_pBtnCamDropdown->setText(QStringLiteral("CAM: OFF ▾"));
        else if (m_cameraSource == 1) m_pBtnCamDropdown->setText(QStringLiteral("CAM 1 ▾"));
        else if (m_cameraSource == 2) m_pBtnCamDropdown->setText(QStringLiteral("CAM 2 ▾"));
        else m_pBtnCamDropdown->setText(QStringLiteral("WIFI CAM ▾"));
    }

    if (m_pBtnLowerThird) m_pBtnLowerThird->setChecked(m_lowerThirdEnabled);
    if (m_pBtnOverlay) m_pBtnOverlay->setChecked(m_overlayEnabled);
    if (m_pBtnRgbSplash) m_pBtnRgbSplash->setChecked(m_rgbSplashEnabled);
    if (m_pBtnProjector) m_pBtnProjector->setChecked(m_pProjectorWindow && m_pProjectorWindow->isVisible());
}

void MasterVideoWidget::updateToolbarGeometry() {
    if (!m_pToolbarContainer) {
        return;
    }
    const int barHeight = 42;
    int y = height() - barHeight - 2;
    if (y < 0) {
        y = 0;
    }
    m_pToolbarContainer->setGeometry(2, y, qMax(140, width() - 4), qMin(barHeight, height()));
    m_pToolbarContainer->setVisible(m_toolbarPinned || underMouse());
}

void MasterVideoWidget::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    updateToolbarGeometry();
}

void MasterVideoWidget::enterEvent(QEvent* event) {
    QWidget::enterEvent(event);
    if (m_pToolbarContainer) {
        m_pToolbarContainer->show();
    }
}

void MasterVideoWidget::leaveEvent(QEvent* event) {
    QWidget::leaveEvent(event);
    if (m_pToolbarContainer && !m_toolbarPinned) {
        m_pToolbarContainer->hide();
    }
}

void MasterVideoWidget::editLowerThirdText() {
    bool ok1 = false;
    QString title = QInputDialog::getText(
            this,
            QStringLiteral("Lower Third Headline"),
            QStringLiteral("Headline text:"),
            QLineEdit::Normal,
            m_lowerThirdTitle,
            &ok1);
    if (ok1 && !title.trimmed().isEmpty()) {
        m_lowerThirdTitle = title.trimmed();
    }

    bool ok2 = false;
    QString sub = QInputDialog::getText(
            this,
            QStringLiteral("Lower Third Subtitle"),
            QStringLiteral("Subtitle / DJ Handle:"),
            QLineEdit::Normal,
            m_lowerThirdSubtitle,
            &ok2);
    if (ok2) {
        m_lowerThirdSubtitle = sub.trimmed();
    }
    m_lowerThirdEnabled = true;
    if (m_pLowerThirdControl) m_pLowerThirdControl->set(1.0);
    update();
}

void MasterVideoWidget::editSplashText() {
    bool ok = false;
    QString txt = QInputDialog::getMultiLineText(
            this,
            QStringLiteral("RGB Billboard Splash Text"),
            QStringLiteral("Enter full-screen billboard message:"),
            m_rgbSplashText,
            &ok);
    if (ok && !txt.trimmed().isEmpty()) {
        m_rgbSplashText = txt.trimmed();
        m_rgbSplashEnabled = true;
        if (m_pRgbSplashControl) m_pRgbSplashControl->set(1.0);
        update();
    }
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
    syncToolbarStates();
    update();
}
void MasterVideoWidget::toggleProjectorWindow() {
    if (m_pProjectorWindow && m_pProjectorWindow->isVisible()) {
        m_pProjectorWindow->close();
        syncToolbarStates();
        return;
    }

    if (!m_pProjectorWindow) {
        m_pProjectorWindow = new ProjectorWindow(this);
        connect(m_pProjectorWindow, &ProjectorWindow::windowClosed, this, [this]() {
            m_pProjectorWindow->deleteLater();
            m_pProjectorWindow = nullptr;
            syncToolbarStates();
        });
    }

    m_pProjectorWindow->show();
    m_pProjectorWindow->raise();
    m_pProjectorWindow->activateWindow();
    syncToolbarStates();
}

void MasterVideoWidget::mouseDoubleClickEvent(QMouseEvent* event) {
    Q_UNUSED(event);
    toggleProjectorWindow();
}

void MasterVideoWidget::contextMenuEvent(QContextMenuEvent* event) {
    QMenu menu(this);

    QAction* editSplashAct = menu.addAction(QStringLiteral("Edit RGB Billboard Splash Text..."));
    connect(editSplashAct, &QAction::triggered, this, &MasterVideoWidget::editSplashText);

    QAction* editL3Act = menu.addAction(QStringLiteral("Edit Lower Third Text..."));
    connect(editL3Act, &QAction::triggered, this, &MasterVideoWidget::editLowerThirdText);

    QAction* cycleOverlayAct = menu.addAction(QStringLiteral("Cycle Overlay Style (Neon / Scanlines / Phoenix)"));
    connect(cycleOverlayAct, &QAction::triggered, this, [this]() {
        m_overlayStyle = (m_overlayStyle + 1) % 3;
        m_overlayEnabled = true;
        if (m_pOverlayControl) m_pOverlayControl->set(1.0);
        update();
    });

    menu.addSeparator();

    QAction* popOutAct = menu.addAction(
            (m_pProjectorWindow && m_pProjectorWindow->isVisible())
                    ? QStringLiteral("Close Phoenix Projector Window")
                    : QStringLiteral("Pop Out Phoenix Projector Window"));
    connect(popOutAct, &QAction::triggered, this, &MasterVideoWidget::toggleProjectorWindow);

    QAction* pinToolbarAct = menu.addAction(QStringLiteral("Pin Quick Access Bar"));
    pinToolbarAct->setCheckable(true);
    pinToolbarAct->setChecked(m_toolbarPinned);
    connect(pinToolbarAct, &QAction::triggered, this, [this](bool checked) {
        m_toolbarPinned = checked;
        updateToolbarGeometry();
    });

    menu.exec(event->globalPos());
}

void MasterVideoWidget::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);
    QPainter painter(this);
    renderComposite(painter, rect(), false);
}

void MasterVideoWidget::drawCentered(
        QPainter& painter,
        const QRect& targetRect,
        const QImage& img,
        double opacity) {
    if (img.isNull() || opacity <= 0.001) {
        return;
    }

    QSize scaledSize = img.size().scaled(targetRect.size(), Qt::KeepAspectRatioByExpanding);
    int x = targetRect.x() + (targetRect.width() - scaledSize.width()) / 2;
    int y = targetRect.y() + (targetRect.height() - scaledSize.height()) / 2;

    painter.save();
    painter.setOpacity(qBound(0.0, opacity, 1.0));
    painter.drawImage(QRect(x, y, scaledSize.width(), scaledSize.height()), img);
    painter.restore();
}

void MasterVideoWidget::renderCameraLayer(QPainter& painter, const QRect& targetRect) {
    if (m_cameraSource <= 0) {
        return;
    }
    painter.save();
    QLinearGradient bgGrad(targetRect.topLeft(), targetRect.bottomRight());
    bgGrad.setColorAt(0.0, QColor(12, 24, 38));
    bgGrad.setColorAt(1.0, QColor(28, 14, 42));
    painter.fillRect(targetRect, bgGrad);

    painter.setPen(QColor(255, 255, 255, 210));
    QFont f = painter.font();
    f.setPointSize(qMax(10, targetRect.height() / 16));
    f.setBold(true);
    painter.setFont(f);

    QString camLabel = (m_cameraSource == 1)
            ? QStringLiteral("LIVE CAMERA 1 FEED")
            : (m_cameraSource == 2)
                    ? QStringLiteral("LIVE CAMERA 2 FEED")
                    : QStringLiteral("WI-FI HOTSPOT CAMERA STREAM");
    painter.drawText(targetRect, Qt::AlignCenter, camLabel);
    painter.restore();
}

void MasterVideoWidget::renderOverlayLayer(QPainter& painter, const QRect& targetRect) {
    if (!m_overlayEnabled) {
        return;
    }
    painter.save();
    if (m_overlayStyle == 0) {
        QColor neon = QColor::fromHsv(m_rgbPhase, 255, 255);
        QPen pen(neon, qMax(3, targetRect.width() / 160));
        painter.setPen(pen);
        painter.drawRect(targetRect.adjusted(6, 6, -6, -6));
    } else if (m_overlayStyle == 1) {
        painter.setPen(QColor(0, 0, 0, 75));
        for (int y = targetRect.top(); y < targetRect.bottom(); y += 4) {
            painter.drawLine(targetRect.left(), y, targetRect.right(), y);
        }
    } else {
        painter.setPen(QColor(255, 165, 0, 190));
        QFont f = painter.font();
        f.setPointSize(qMax(9, targetRect.height() / 22));
        f.setBold(true);
        painter.setFont(f);
        painter.drawText(targetRect.adjusted(12, 10, -12, -10), Qt::AlignTop | Qt::AlignRight, QStringLiteral("PHOENIX VJ"));
    }
    painter.restore();
}

void MasterVideoWidget::renderLowerThirdLayer(QPainter& painter, const QRect& targetRect) {
    if (!m_lowerThirdEnabled) {
        return;
    }
    painter.save();
    int barH = qMax(34, targetRect.height() / 6);
    int barW = static_cast<int>(targetRect.width() * 0.62);
    int x = targetRect.left() + static_cast<int>(targetRect.width() * 0.04);
    int y = targetRect.bottom() - barH - static_cast<int>(targetRect.height() * 0.14);

    QRect bannerRect(x, y, barW, barH);
    QLinearGradient grad(bannerRect.topLeft(), bannerRect.topRight());
    grad.setColorAt(0.0, QColor(232, 93, 4, 230));
    grad.setColorAt(0.7, QColor(20, 20, 28, 225));
    grad.setColorAt(1.0, QColor(20, 20, 28, 0));

    painter.setPen(Qt::NoPen);
    painter.setBrush(grad);
    painter.drawRoundedRect(bannerRect, 4, 4);

    painter.setBrush(QColor::fromHsv(m_rgbPhase, 240, 255));
    painter.drawRect(QRect(x, y, 4, barH));

    painter.setPen(Qt::white);
    QFont titleFont = painter.font();
    titleFont.setPointSize(qMax(9, barH / 3));
    titleFont.setBold(true);
    painter.setFont(titleFont);
    painter.drawText(
            QRect(x + 12, y + 2, barW - 16, barH / 2),
            Qt::AlignLeft | Qt::AlignVCenter,
            m_lowerThirdTitle);

    painter.setPen(QColor(255, 200, 120));
    QFont subFont = painter.font();
    subFont.setPointSize(qMax(7, barH / 4));
    subFont.setBold(false);
    painter.setFont(subFont);
    painter.drawText(
            QRect(x + 12, y + (barH / 2), barW - 16, barH / 2 - 2),
            Qt::AlignLeft | Qt::AlignVCenter,
            m_lowerThirdSubtitle);
    painter.restore();
}

void MasterVideoWidget::renderRgbSplashLayer(QPainter& painter, const QRect& targetRect) {
    if (!m_rgbSplashEnabled || m_rgbSplashText.isEmpty()) {
        return;
    }
    painter.save();

    painter.fillRect(targetRect, QColor(5, 5, 10, 195));

    const int cols = qBound(48, targetRect.width() / 7, 140);
    const int rows = qBound(24, targetRect.height() / 7, 80);

    QImage textMask(cols, rows, QImage::Format_ARGB32_Premultiplied);
    textMask.fill(Qt::transparent);

    {
        QPainter maskPainter(&textMask);
        maskPainter.setPen(Qt::white);

        int fontSize = qMax(8, rows / 3);
        QFont ledFont(QStringLiteral("Arial Black"), fontSize, QFont::Bold);
        ledFont.setStyleStrategy(QFont::NoAntialias);
        maskPainter.setFont(ledFont);

        QRect maskRect(2, 2, cols - 4, rows - 4);
        QRect bounds = maskPainter.boundingRect(maskRect, Qt::AlignCenter | Qt::TextWordWrap, m_rgbSplashText);
        while ((bounds.width() > cols - 4 || bounds.height() > rows - 4) && fontSize > 6) {
            fontSize--;
            ledFont.setPointSize(fontSize);
            maskPainter.setFont(ledFont);
            bounds = maskPainter.boundingRect(maskRect, Qt::AlignCenter | Qt::TextWordWrap, m_rgbSplashText);
        }

        maskPainter.drawText(maskRect, Qt::AlignCenter | Qt::TextWordWrap, m_rgbSplashText);
    }

    const double cellW = static_cast<double>(targetRect.width()) / cols;
    const double cellH = static_cast<double>(targetRect.height()) / rows;
    const double dotRadius = qMin(cellW, cellH) * 0.38;

    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);

    for (int r = 0; r < rows; ++r) {
        const QRgb* scanLine = reinterpret_cast<const QRgb*>(textMask.constScanLine(r));
        for (int c = 0; c < cols; ++c) {
            double cx = targetRect.left() + (c + 0.5) * cellW;
            double cy = targetRect.top() + (r + 0.5) * cellH;
            bool isLit = qAlpha(scanLine[c]) > 100;

            if (isLit) {
                int hue = (m_rgbPhase + (c * 5) + (r * 7)) % 360;
                QColor bulbColor = QColor::fromHsv(hue, 255, 255);
                QColor glowColor = QColor::fromHsv(hue, 220, 255, 95);

                painter.setBrush(glowColor);
                painter.drawEllipse(QPointF(cx, cy), dotRadius * 1.55, dotRadius * 1.55);

                painter.setBrush(bulbColor);
                painter.drawEllipse(QPointF(cx, cy), dotRadius, dotRadius);
            } else {
                painter.setBrush(QColor(28, 28, 36, 120));
                painter.drawEllipse(QPointF(cx, cy), dotRadius * 0.65, dotRadius * 0.65);
            }
        }
    }

    painter.restore();
}

void MasterVideoWidget::renderComposite(QPainter& painter, const QRect& targetRect, bool isProjectorOutput) {
    painter.fillRect(targetRect, Qt::black);

    if (m_cameraSource > 0) {
        renderCameraLayer(painter, targetRect);
    } else {
        VideoWidget* deck1 = VideoWidget::getDeckWidget(QStringLiteral("[Channel1]"));
        VideoWidget* deck2 = VideoWidget::getDeckWidget(QStringLiteral("[Channel2]"));

        QImage img1 = deck1 ? deck1->currentFrame() : QImage();
        QImage img2 = deck2 ? deck2->currentFrame() : QImage();

        double xfader = m_pCrossfader ? qBound(-1.0, m_pCrossfader->get(), 1.0) : 0.0;
        double mix = (xfader + 1.0) * 0.5;
        double vol1 = m_pVol1 ? qBound(0.0, m_pVol1->get(), 1.0) : 1.0;
        double vol2 = m_pVol2 ? qBound(0.0, m_pVol2->get(), 1.0) : 1.0;

        if (isProjectorOutput && m_cueTransition) {
            if (xfader <= 0.0) {
                drawCentered(painter, targetRect, img1, vol1);
            } else {
                drawCentered(painter, targetRect, img2, vol2);
            }
        } else {
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
    }

    renderOverlayLayer(painter, targetRect);
    renderLowerThirdLayer(painter, targetRect);
    renderRgbSplashLayer(painter, targetRect);

    if (!isProjectorOutput && m_cueTransition) {
        painter.save();
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(217, 83, 79, 220));
        painter.drawRoundedRect(QRect(targetRect.x() + 6, targetRect.y() + 6, 74, 16), 3, 3);
        painter.setPen(Qt::white);
        QFont f = painter.font();
        f.setPointSize(8);
        f.setBold(true);
        painter.setFont(f);
        painter.drawText(QRect(targetRect.x() + 6, targetRect.y() + 6, 74, 16), Qt::AlignCenter, QStringLiteral("CUE PREVIEW"));
        painter.restore();
    }
}
