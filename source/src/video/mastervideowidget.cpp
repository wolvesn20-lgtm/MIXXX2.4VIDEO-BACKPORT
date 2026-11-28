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
#include <QScreen>

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
            int modeVal = qBound(0, static_cast<int>(m_pTransitionControl->get()), 7);
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
                int nextMode = QRandomGenerator::global()->bounded(8);
                setTransitionMode(static_cast<TransitionMode>(nextMode));
            }
            prevBeat = beatHit;
        }

        syncToolbarStates();
        update();
        if (m_pProjectorWindow && m_pProjectorWindow->isVisible()) {
            m_pProjectorWindow->update();
        }
    });
    pRefreshTimer->start(33);
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
void MasterVideoWidget::setupUniqueButtonContextMenus() {
    auto bindMenu = [](QWidget* w, std::function<void(QMenu&)> builder) {
        if (!w) return;
        w->setContextMenuPolicy(Qt::CustomContextMenu);
        QObject::connect(w, &QWidget::customContextMenuRequested, w, [w, builder](const QPoint& pos) {
            QMenu menu(w);
            builder(menu);
            menu.exec(w->mapToGlobal(pos));
        });
    };

    // 1. LOWER 3RD Right-Click Menu (Styles, Presets, Font, Scale, Color, Position)
    bindMenu(m_pBtnLowerThird, [this](QMenu& menu) {
        menu.addAction(QStringLiteral("Edit Lower Third Text..."), this, &MasterVideoWidget::editLowerThirdText);

        QMenu* styleMenu = menu.addMenu(QStringLiteral("Banner Style"));
        styleMenu->addAction(QStringLiteral("1. Phoenix Gradient Bar"), [this]() { m_lowerThirdStyle = 0; m_lowerThirdEnabled = true; update(); });
        styleMenu->addAction(QStringLiteral("2. Rounded Club Pill"), [this]() { m_lowerThirdStyle = 1; m_lowerThirdEnabled = true; update(); });
        styleMenu->addAction(QStringLiteral("3. Minimalist Accent Line"), [this]() { m_lowerThirdStyle = 2; m_lowerThirdEnabled = true; update(); });
        styleMenu->addAction(QStringLiteral("4. Neon Glow Box"), [this]() { m_lowerThirdStyle = 3; m_lowerThirdEnabled = true; update(); });

        QMenu* presetMenu = menu.addMenu(QStringLiteral("Quick Text Presets"));
        presetMenu->addAction(QStringLiteral("PHOENIX LIVE / The Night Wolves Ent"), [this]() {
            m_lowerThirdTitle = QStringLiteral("PHOENIX LIVE");
            m_lowerThirdSubtitle = QStringLiteral("The Night Wolves Ent");
            m_lowerThirdEnabled = true; update();
        });
        presetMenu->addAction(QStringLiteral("NOW MIXING / Live Video Set"), [this]() {
            m_lowerThirdTitle = QStringLiteral("NOW MIXING");
            m_lowerThirdSubtitle = QStringLiteral("Live Video Set");
            m_lowerThirdEnabled = true; update();
        });
        presetMenu->addAction(QStringLiteral("REQUESTS OPEN / DM @NightWolves"), [this]() {
            m_lowerThirdTitle = QStringLiteral("REQUESTS OPEN");
            m_lowerThirdSubtitle = QStringLiteral("Shoutouts & Requests");
            m_lowerThirdEnabled = true; update();
        });

        menu.addSeparator();
        menu.addAction(QStringLiteral("Choose Font..."), [this]() {
            bool ok = false;
            QFont f = QFontDialog::getFont(&ok, QFont(m_lowerThirdFontFamily, 16), this);
            if (ok) { m_lowerThirdFontFamily = f.family(); update(); }
        });
        menu.addAction(QStringLiteral("Choose Accent Color..."), [this]() {
            QColor c = QColorDialog::getColor(m_lowerThirdAccentColor, this);
            if (c.isValid()) { m_lowerThirdAccentColor = c; update(); }
        });

        QMenu* scaleMenu = menu.addMenu(QStringLiteral("Text & Banner Scale"));
        for (double s : {0.75, 1.0, 1.25, 1.5}) {
            scaleMenu->addAction(QStringLiteral("%1%").arg(qRound(s * 100)), [this, s]() { m_lowerThirdScale = s; update(); });
        }

        QMenu* posMenu = menu.addMenu(QStringLiteral("Snap Position"));
        posMenu->addAction(QStringLiteral("Bottom Left"), [this]() { m_lowerThirdPos = QPointF(0.04, 0.78); update(); });
        posMenu->addAction(QStringLiteral("Bottom Center"), [this]() { m_lowerThirdPos = QPointF(0.19, 0.78); update(); });
        posMenu->addAction(QStringLiteral("Bottom Right"), [this]() { m_lowerThirdPos = QPointF(0.34, 0.78); update(); });
        posMenu->addAction(QStringLiteral("Top Left"), [this]() { m_lowerThirdPos = QPointF(0.04, 0.06); update(); });
    });

    // 2. OVERLAY Right-Click Menu
    bindMenu(m_pBtnOverlay, [this](QMenu& menu) {
        menu.addAction(QStringLiteral("Style: RGB Neon Border"), [this]() { m_overlayStyle = 0; m_overlayEnabled = true; update(); });
        menu.addAction(QStringLiteral("Style: Retro CRT Scanlines"), [this]() { m_overlayStyle = 1; m_overlayEnabled = true; update(); });
        menu.addAction(QStringLiteral("Style: Floating Watermark Text"), [this]() { m_overlayStyle = 2; m_overlayEnabled = true; update(); });
        menu.addSeparator();
        menu.addAction(QStringLiteral("Edit Watermark Text..."), [this]() {
            bool ok = false;
            QString t = QInputDialog::getText(this, QStringLiteral("Watermark Text"), QStringLiteral("Text:"), QLineEdit::Normal, m_overlayText, &ok);
            if (ok && !t.trimmed().isEmpty()) { m_overlayText = t.trimmed(); m_overlayStyle = 2; m_overlayEnabled = true; update(); }
        });
        menu.addAction(QStringLiteral("Choose Watermark Font..."), [this]() {
            bool ok = false;
            QFont f = QFontDialog::getFont(&ok, QFont(m_overlayFontFamily, 16), this);
            if (ok) { m_overlayFontFamily = f.family(); update(); }
        });
        menu.addAction(QStringLiteral("Choose Watermark Color..."), [this]() {
            QColor c = QColorDialog::getColor(m_overlayColor, this);
            if (c.isValid()) { m_overlayColor = c; update(); }
        });
        QMenu* scaleMenu = menu.addMenu(QStringLiteral("Watermark Scale"));
        for (double s : {0.75, 1.0, 1.35, 1.75}) {
            scaleMenu->addAction(QStringLiteral("%1%").arg(qRound(s * 100)), [this, s]() { m_overlayScale = s; update(); });
        }
        QMenu* posMenu = menu.addMenu(QStringLiteral("Watermark Position"));
        posMenu->addAction(QStringLiteral("Top Right"), [this]() { m_overlayTextPos = QPointF(0.72, 0.06); update(); });
        posMenu->addAction(QStringLiteral("Top Left"), [this]() { m_overlayTextPos = QPointF(0.04, 0.06); update(); });
        posMenu->addAction(QStringLiteral("Bottom Right"), [this]() { m_overlayTextPos = QPointF(0.72, 0.85); update(); });
        posMenu->addAction(QStringLiteral("Center"), [this]() { m_overlayTextPos = QPointF(0.38, 0.45); update(); });
    });

    // 3. RGB SPLASH Right-Click Menu
    bindMenu(m_pBtnRgbSplash, [this](QMenu& menu) {
        menu.addAction(QStringLiteral("Edit Billboard Message..."), this, &MasterVideoWidget::editSplashText);
        menu.addAction(QStringLiteral("Choose Billboard Font..."), [this]() {
            bool ok = false;
            QFont f = QFontDialog::getFont(&ok, QFont(m_rgbSplashFontFamily, 18), this);
            if (ok) { m_rgbSplashFontFamily = f.family(); update(); }
        });
        QMenu* scaleMenu = menu.addMenu(QStringLiteral("LED Matrix Density / Scale"));
        scaleMenu->addAction(QStringLiteral("Fine LEDs (High Res)"), [this]() { m_rgbSplashScale = 1.4; update(); });
        scaleMenu->addAction(QStringLiteral("Standard LEDs"), [this]() { m_rgbSplashScale = 1.0; update(); });
        scaleMenu->addAction(QStringLiteral("Chunky Club LEDs"), [this]() { m_rgbSplashScale = 0.7; update(); });
    });

    // 4. CAMERA Right-Click Menu
    connect(m_pBtnCamDropdown, &QPushButton::clicked, this, [this]() {
    QMenu camMenu(this);
    QAction* offAct = camMenu.addAction(QStringLiteral("Camera Off (Decks Only)"));
    offAct->setCheckable(true);
    offAct->setChecked(m_cameraSource == 0);

    const QList<QCameraInfo> cameras = QCameraInfo::availableCameras();
    QList<QAction*> camActions;
    for (int i = 0; i < cameras.size(); ++i) {
        QAction* act = camMenu.addAction(QStringLiteral("Cam %1: %2").arg(i + 1).arg(cameras[i].description()));
        act->setCheckable(true);
        act->setChecked(m_cameraSource == (i + 1));
        camActions.append(act);
    }

    camMenu.addSeparator();
    QAction* pipAct = camMenu.addAction(QStringLiteral("Picture-in-Picture (PiP) Mode"));
    pipAct->setCheckable(true);
    pipAct->setChecked(m_cameraPipMode);

    QAction* chosen = camMenu.exec(m_pBtnCamDropdown->mapToGlobal(QPoint(0, m_pBtnCamDropdown->height())));
    if (!chosen) return;

    if (chosen == offAct) {
        stopCameraDevice();
    } else if (chosen == pipAct) {
        m_cameraPipMode = !m_cameraPipMode;
    } else {
        int idx = camActions.indexOf(chosen);
        if (idx >= 0) {
            startCameraDevice(idx);
        }
    }
    syncToolbarStates();
    update();
});

void MasterVideoWidget::startWifiCamera(const QString& streamUrl) {
    stopCameraDevice();
    if (!m_pNetManager) {
        m_pNetManager = new QNetworkAccessManager(this);
    }

    m_wifiCamUrl = streamUrl;
    m_mjpegBuffer.clear();
    m_cameraSource = 1;
    if (m_pCameraSourceControl) {
        m_pCameraSourceControl->set(1.0);
    }

    QNetworkRequest request{QUrl(m_wifiCamUrl)};
    request.setRawHeader("User-Agent", "PhoenixVJ-Mixxx/2.4");
    m_pWifiCamReply = m_pNetManager->get(request);

    connect(m_pWifiCamReply, &QNetworkReply::readyRead, this, [this]() {
        if (!m_pWifiCamReply) return;
        m_mjpegBuffer.append(m_pWifiCamReply->readAll());

        if (m_mjpegBuffer.size() > 2 * 1024 * 1024) {
            m_mjpegBuffer.clear();
            return;
        }

        const QByteArray soi("\xff\xd8", 2);
        const QByteArray eoi("\xff\xd9", 2);

        while (true) {
            int start = m_mjpegBuffer.indexOf(soi);
            if (start < 0) {
                m_mjpegBuffer.clear();
                break;
            }
            int end = m_mjpegBuffer.indexOf(eoi, start + 2);
            if (end < 0) {
                if (start > 0) m_mjpegBuffer.remove(0, start);
                break;
            }

            QByteArray jpegData = m_mjpegBuffer.mid(start, (end + 2) - start);
            m_mjpegBuffer.remove(0, end + 2);

            QImage frame;
            if (frame.loadFromData(jpegData, "JPEG")) {
                m_latestCameraFrame = frame;
            }
        }
    });

    connect(m_pWifiCamReply, &QNetworkReply::finished, this, [this]() {
        if (m_pWifiCamReply) {
            m_pWifiCamReply->deleteLater();
            m_pWifiCamReply = nullptr;
        }
    });
}

void MasterVideoWidget::stopCameraDevice() {
    if (m_pWifiCamReply) {
        m_pWifiCamReply->abort();
        m_pWifiCamReply->deleteLater();
        m_pWifiCamReply = nullptr;
    }
    m_mjpegBuffer.clear();
    m_latestCameraFrame = QImage();
    m_cameraSource = 0;
    if (m_pCameraSourceControl) {
        m_pCameraSourceControl->set(0.0);
    }
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
    m_pBtnRec = new QPushButton(QStringLiteral("● REC"), m_pToolbarContainer);
        
m_pBtnRec->setCheckable(true);
m_pBtnRec->setStyleSheet(QStringLiteral(
        "QPushButton:checked { background-color: #D00000; color: #FFFFFF; border-color: #FF4D4D; }"));
row1->addWidget(m_pBtnRec, 1);

connect(m_pBtnRec, &QPushButton::toggled, this, [this](bool checked) {
    m_isRecording = checked;
    update();
});

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
        m_lastCameraPipRect = QRect();
        return;
    }

    QRect drawRect = targetRect;
    if (m_cameraPipMode) {
        int pipW = qRound(targetRect.width() * m_cameraPipSize.width());
        int pipH = qRound(targetRect.height() * m_cameraPipSize.height());
        int pipX = targetRect.left() + qRound(m_cameraPipPos.x() * targetRect.width());
        int pipY = targetRect.top() + qRound(m_cameraPipPos.y() * targetRect.height());
        drawRect = QRect(pipX, pipY, pipW, pipH);
        m_lastCameraPipRect = drawRect;
    } else {
        m_lastCameraPipRect = QRect();
    }

    painter.save();
    if (!m_latestCameraFrame.isNull()) {
        drawCentered(painter, drawRect, m_latestCameraFrame, 1.0);
    } else {
        painter.fillRect(drawRect, QColor(14, 18, 28));
        painter.setPen(Qt::white);
        painter.drawText(drawRect, Qt::AlignCenter, QStringLiteral("CONNECTING CAMERA..."));
    }

    if (m_cameraPipMode) {
        painter.setPen(QPen(m_lowerThirdAccentColor, qMax(2, targetRect.width() / 320)));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(drawRect);
    }
    painter.restore();
}

void MasterVideoWidget::renderOverlayLayer(QPainter& painter, const QRect& targetRect) {
    if (!m_overlayEnabled) {
        m_lastOverlayTextRect = QRect();
        return;
    }
    painter.save();
    double scale = qMax(0.25, static_cast<double>(targetRect.height()) / 720.0) * m_overlayScale;

    if (m_overlayStyle == 0) {
        m_lastOverlayTextRect = QRect();
        QColor neon = QColor::fromHsv(m_rgbPhase, 255, 255);
        QPen pen(neon, qMax(3, qRound(6.0 * scale)));
        painter.setPen(pen);
        painter.drawRect(targetRect.adjusted(6, 6, -6, -6));
    } else if (m_overlayStyle == 1) {
        m_lastOverlayTextRect = QRect();
        painter.setPen(QColor(0, 0, 0, 75));
        for (int y = targetRect.top(); y < targetRect.bottom(); y += 4) {
            painter.drawLine(targetRect.left(), y, targetRect.right(), y);
        }
    } else {
        QFont f(m_overlayFontFamily);
        f.setPixelSize(qMax(10, qRound(28.0 * scale)));
        f.setBold(true);
        painter.setFont(f);

        QFontMetrics fm(f);
        int textW = fm.horizontalAdvance(m_overlayText) + qRound(16.0 * scale);
        int textH = fm.height() + qRound(8.0 * scale);
        int x = targetRect.left() + qRound(m_overlayTextPos.x() * targetRect.width());
        int y = targetRect.top() + qRound(m_overlayTextPos.y() * targetRect.height());

        m_lastOverlayTextRect = QRect(x, y, textW, textH);
        painter.setPen(m_overlayColor);
        painter.drawText(m_lastOverlayTextRect, Qt::AlignCenter, m_overlayText);
    }
    painter.restore();
}

void MasterVideoWidget::renderLowerThirdLayer(QPainter& painter, const QRect& targetRect) {
    if (!m_lowerThirdEnabled) {
        m_lastLowerThirdRect = QRect();
        return;
    }
    painter.save();
    double scale = qMax(0.25, static_cast<double>(targetRect.height()) / 720.0) * m_lowerThirdScale;

    int barH = qMax(32, qRound(92.0 * scale));
    int barW = qBound(140, qRound(targetRect.width() * 0.60 * m_lowerThirdScale), qRound(targetRect.width() * 0.92));
    int x = targetRect.left() + qRound(m_lowerThirdPos.x() * targetRect.width());
    int y = targetRect.top() + qRound(m_lowerThirdPos.y() * targetRect.height());

    QRect bannerRect(x, y, barW, barH);
    m_lastLowerThirdRect = bannerRect;

    if (m_lowerThirdStyle == 1) {
        // Style 1: Rounded Club Pill
        painter.setPen(QPen(m_lowerThirdAccentColor, qMax(2, qRound(3.0 * scale))));
        painter.setBrush(QColor(15, 15, 22, 225));
        painter.drawRoundedRect(bannerRect, barH / 2.0, barH / 2.0);
    } else if (m_lowerThirdStyle == 2) {
        // Style 2: Minimalist Accent Line
        painter.setPen(Qt::NoPen);
        painter.setBrush(m_lowerThirdAccentColor);
        painter.drawRect(QRect(x, y, qMax(4, qRound(6.0 * scale)), barH));
    } else if (m_lowerThirdStyle == 3) {
        // Style 3: Neon Glow Box
        QColor neon = QColor::fromHsv(m_rgbPhase, 255, 255);
        painter.setPen(QPen(neon, qMax(2, qRound(3.0 * scale))));
        painter.setBrush(QColor(10, 10, 16, 215));
        painter.drawRect(bannerRect);
    } else {
        // Style 0: Default Phoenix Gradient Bar
        QLinearGradient grad(bannerRect.topLeft(), bannerRect.topRight());
        grad.setColorAt(0.0, QColor(m_lowerThirdAccentColor.red(), m_lowerThirdAccentColor.green(), m_lowerThirdAccentColor.blue(), 230));
        grad.setColorAt(0.7, QColor(20, 20, 28, 225));
        grad.setColorAt(1.0, QColor(20, 20, 28, 0));
        painter.setPen(Qt::NoPen);
        painter.setBrush(grad);
        painter.drawRoundedRect(bannerRect, 4, 4);

        painter.setBrush(QColor::fromHsv(m_rgbPhase, 240, 255));
        painter.drawRect(QRect(x, y, qMax(3, qRound(5.0 * scale)), barH));
    }

    int padLeft = qRound(16.0 * scale);
    int maxTextW = barW - padLeft - qRound(12.0 * scale);

    // Title Font with Auto-Fit Shrinking
    QFont titleFont(m_lowerThirdFontFamily);
    titleFont.setPixelSize(qMax(10, qRound(32.0 * scale)));
    titleFont.setBold(true);
    QFontMetrics fmTitle(titleFont);
    if (fmTitle.horizontalAdvance(m_lowerThirdTitle) > maxTextW && maxTextW > 20) {
        double ratio = static_cast<double>(maxTextW) / fmTitle.horizontalAdvance(m_lowerThirdTitle);
        titleFont.setPixelSize(qMax(9, qRound(titleFont.pixelSize() * ratio)));
    }
    painter.setPen(Qt::white);
    painter.setFont(titleFont);
    painter.drawText(QRect(x + padLeft, y + 2, maxTextW, barH / 2), Qt::AlignLeft | Qt::AlignVCenter, m_lowerThirdTitle);

    // Subtitle Font with Auto-Fit Shrinking
    QFont subFont(m_lowerThirdFontFamily);
    subFont.setPixelSize(qMax(8, qRound(20.0 * scale)));
    QFontMetrics fmSub(subFont);
    if (fmSub.horizontalAdvance(m_lowerThirdSubtitle) > maxTextW && maxTextW > 20) {
        double ratio = static_cast<double>(maxTextW) / fmSub.horizontalAdvance(m_lowerThirdSubtitle);
        subFont.setPixelSize(qMax(8, qRound(subFont.pixelSize() * ratio)));
    }
    painter.setPen(QColor(255, 220, 160));
    painter.setFont(subFont);
    painter.drawText(QRect(x + padLeft, y + (barH / 2), maxTextW, barH / 2 - 2), Qt::AlignLeft | Qt::AlignVCenter, m_lowerThirdSubtitle);
    painter.restore();
}

void MasterVideoWidget::renderRecBadge(QPainter& painter, const QRect& targetRect) {
    if (!m_isRecording) return;
    painter.save();
    double scale = qMax(0.3, static_cast<double>(targetRect.height()) / 720.0);
    int badgeW = qRound(95.0 * scale);
    int badgeH = qRound(30.0 * scale);
    QRect badgeRect(targetRect.left() + qRound(16.0 * scale), targetRect.top() + qRound(16.0 * scale), badgeW, badgeH);

    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(15, 15, 20, 200));
    painter.drawRoundedRect(badgeRect, 6, 6);

    // Pulsing red dot
    int alpha = (m_rgbPhase % 180 < 90) ? 255 : 90;
    painter.setBrush(QColor(230, 20, 20, alpha));
    int dotSize = qRound(12.0 * scale);
    painter.drawEllipse(badgeRect.left() + qRound(10.0 * scale), badgeRect.center().y() - dotSize / 2, dotSize, dotSize);

    painter.setPen(Qt::white);
    QFont f(QStringLiteral("Arial"), qMax(8, qRound(14.0 * scale)), QFont::Bold);
    painter.setFont(f);
    painter.drawText(badgeRect.adjusted(dotSize + qRound(14.0 * scale), 0, -4, 0), Qt::AlignVCenter | Qt::AlignLeft, QStringLiteral("REC"));
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

}
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
void MasterVideoWidget::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        if (m_lowerThirdEnabled && m_lastLowerThirdRect.contains(event->pos())) {
            m_dragTarget = DragTarget::LowerThird;
            m_dragOffset = QPointF(event->pos() - m_lastLowerThirdRect.topLeft());
            setCursor(Qt::ClosedHandCursor);
            return;
        }
        if (m_overlayEnabled && m_overlayStyle == 2 && m_lastOverlayTextRect.contains(event->pos())) {
            m_dragTarget = DragTarget::OverlayText;
            m_dragOffset = QPointF(event->pos() - m_lastOverlayTextRect.topLeft());
            setCursor(Qt::ClosedHandCursor);
            return;
        }
        if (m_cameraSource > 0 && m_cameraPipMode && m_lastCameraPipRect.contains(event->pos())) {
            m_dragTarget = DragTarget::CameraPip;
            m_dragOffset = QPointF(event->pos() - m_lastCameraPipRect.topLeft());
            setCursor(Qt::ClosedHandCursor);
            return;
        }
    }
    QWidget::mousePressEvent(event);
}

void MasterVideoWidget::mouseMoveEvent(QMouseEvent* event) {
    if (m_dragTarget != DragTarget::None && width() > 0 && height() > 0) {
        QPointF newTopLeft = QPointF(event->pos()) - m_dragOffset;
        double normX = qBound(0.0, newTopLeft.x() / width(), 0.90);
        double normY = qBound(0.0, newTopLeft.y() / height(), 0.90);

        if (m_dragTarget == DragTarget::LowerThird)       m_lowerThirdPos = QPointF(normX, normY);
        else if (m_dragTarget == DragTarget::OverlayText) m_overlayTextPos = QPointF(normX, normY);
        else if (m_dragTarget == DragTarget::CameraPip)   m_cameraPipPos = QPointF(normX, normY);

        update();
        return;
    }
    QWidget::mouseMoveEvent(event);
}

void MasterVideoWidget::mouseReleaseEvent(QMouseEvent* event) {
    if (m_dragTarget != DragTarget::None) {
        m_dragTarget = DragTarget::None;
        setCursor(Qt::ArrowCursor);
    }
    QWidget::mouseReleaseEvent(event);
}
