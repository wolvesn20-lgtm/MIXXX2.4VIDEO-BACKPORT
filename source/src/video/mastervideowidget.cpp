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

