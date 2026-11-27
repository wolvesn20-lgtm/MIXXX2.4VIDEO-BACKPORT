#pragma once

#include <QByteArray>
#include <QColor>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QEvent>
#include <QImage>
#include <QMouseEvent>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPaintEvent>
#include <QPainter>
#include <QPointF>
#include <QPushButton>
#include <QRect>
#include <QResizeEvent>
#include <QSizeF>
#include <QString>
#include <QWidget>

#include "control/controlobject.h"
#include "control/controlproxy.h"

class MasterVideoWidget;

class ProjectorWindow : public QWidget {
    Q_OBJECT
  public:
    explicit ProjectorWindow(MasterVideoWidget* pMaster, QWidget* parent = nullptr);

  protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void closeEvent(QCloseEvent* event) override;

  signals:
    void windowClosed();

  private:
    MasterVideoWidget* m_pMaster{nullptr};
    bool m_isCustomFullScreen{false};
    QRect m_savedGeometry;
};

class MasterVideoWidget : public QWidget {
    Q_OBJECT
  public:
    enum class TransitionMode {
        Mix = 0,
        WipeHorizontal = 1,
        WipeVertical = 2,
        Cut = 3,
        Zoom = 4,
        LumaFade = 5,
        SlidePush = 6,
        RgbGlitch = 7
    };
    Q_ENUM(TransitionMode)

    explicit MasterVideoWidget(QWidget* parent = nullptr);
    ~MasterVideoWidget() override;

    void renderComposite(QPainter& painter, const QRect& targetRect, bool isProjectorOutput = false);

  public slots:
    void toggleProjectorWindow();
    void setTransitionMode(TransitionMode mode);
    void triggerAutoTransition();
    void editSplashText();
    void editLowerThirdText();
    void startWifiCamera(const QString& streamUrl);
    void stopCameraDevice();

  protected:
    void enterEvent(QEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

  private:
    void buildQuickToolbar();
    void setupUniqueButtonContextMenus();
    void syncToolbarStates();
    void updateToolbarGeometry();
    void drawCentered(QPainter& painter, const QRect& targetRect, const QImage& img, double opacity);
    void renderCameraLayer(QPainter& painter, const QRect& targetRect);
    void renderOverlayLayer(QPainter& painter, const QRect& targetRect);
    void renderLowerThirdLayer(QPainter& painter, const QRect& targetRect);
    void renderRgbSplashLayer(QPainter& painter, const QRect& targetRect);
    void renderRecBadge(QPainter& painter, const QRect& targetRect);

    ControlProxy* m_pCrossfader{nullptr};
    ControlProxy* m_pVol1{nullptr};
    ControlProxy* m_pVol2{nullptr};
    ControlProxy* m_pPlay1{nullptr};
    ControlProxy* m_pPlay2{nullptr};
    ControlProxy* m_pBeatActive1{nullptr};
    ControlProxy* m_pBeatActive2{nullptr};

    ControlObject* m_pTransitionControl{nullptr};
    ControlObject* m_pUpfaderPreviewControl{nullptr};
    ControlObject* m_pCueTransitionControl{nullptr};
    ControlObject* m_pAutoPlayControl{nullptr};
    ControlObject* m_pRandomTransitionControl{nullptr};
    ControlObject* m_pBeatSyncControl{nullptr};
    ControlObject* m_pCameraSourceControl{nullptr};
    ControlObject* m_pLowerThirdControl{nullptr};
    ControlObject* m_pOverlayControl{nullptr};
    ControlObject* m_pRgbSplashControl{nullptr};

    TransitionMode m_transition{TransitionMode::Mix};
    ProjectorWindow* m_pProjectorWindow{nullptr};

    bool m_cueTransition{false};
    bool m_autoPlayEnabled{true};
    bool m_randomTransition{false};
    bool m_beatSyncEnabled{false};

    // --- Wi-Fi Camera State ---
    int m_cameraSource{0}; // 0 = Off, 1 = Wi-Fi Stream Active
    bool m_cameraPipMode{false};
    QNetworkAccessManager* m_pNetManager{nullptr};
    QNetworkReply* m_pWifiCamReply{nullptr};
    QByteArray m_mjpegBuffer;
    QString m_wifiCamUrl{QStringLiteral("http://192.168.43")};
    QImage m_latestCameraFrame;

    // --- REC State ---
    bool m_isRecording{false};

    // --- Lower Third State & Styling ---
    bool m_lowerThirdEnabled{false};
    int m_lowerThirdStyle{0}; // 0 = Phoenix Bar, 1 = Club Pill, 2 = Minimal Line, 3 = Neon Box
    QString m_lowerThirdTitle{QStringLiteral("PHOENIX LIVE")};
    QString m_lowerThirdSubtitle{QStringLiteral("The Night Wolves Ent")};
    QString m_lowerThirdFontFamily{QStringLiteral("Arial Black")};
    double m_lowerThirdScale{1.0};
    QColor m_lowerThirdAccentColor{QColor(232, 93, 4)};

    // --- Overlay & Splash Styling ---
    bool m_overlayEnabled{false};
    int m_overlayStyle{0};
    QString m_overlayText{QStringLiteral("PHOENIX VJ")};
    QString m_overlayFontFamily{QStringLiteral("Arial Black")};
    double m_overlayScale{1.0};
    QColor m_overlayColor{QColor(255, 165, 0)};

    bool m_rgbSplashEnabled{false};
    QString m_rgbSplashText{QStringLiteral("PHOENIX POWERED BY THE NIGHT WOLVES ENT")};
    QString m_rgbSplashFontFamily{QStringLiteral("Arial Black")};
    double m_rgbSplashScale{1.0};
    int m_rgbPhase{0};

    // --- Normalized Positioning (0.0 to 1.0) & Mouse Dragging ---
    QPointF m_lowerThirdPos{QPointF(0.04, 0.78)};
    QPointF m_overlayTextPos{QPointF(0.72, 0.06)};
    QPointF m_cameraPipPos{QPointF(0.70, 0.06)};
    QSizeF m_cameraPipSize{QSizeF(0.26, 0.26)};

    QRect m_lastLowerThirdRect;
    QRect m_lastOverlayTextRect;
    QRect m_lastCameraPipRect;

    enum class DragTarget { None, LowerThird, OverlayText, CameraPip };
    DragTarget m_dragTarget{DragTarget::None};
    QPointF m_dragOffset;

    bool m_toolbarPinned{true};
    bool m_autoFadeActive{false};
    double m_autoFadeProgress{0.0};
    double m_autoFadeStart{0.0};
    double m_autoFadeTarget{1.0};

    QWidget* m_pToolbarContainer{nullptr};
    QComboBox* m_pTransitionDropdown{nullptr};
    QPushButton* m_pBtnFadeNow{nullptr};
    QPushButton* m_pBtnProjector{nullptr};
    QPushButton* m_pBtnRec{nullptr};
    QPushButton* m_pBtnCamDropdown{nullptr};
    QPushButton* m_pBtnLowerThird{nullptr};
    QPushButton* m_pBtnOverlay{nullptr};
    QPushButton* m_pBtnRgbSplash{nullptr};
};
