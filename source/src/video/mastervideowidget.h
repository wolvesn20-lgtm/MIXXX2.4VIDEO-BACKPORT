#pragma once

#include <QComboBox>
#include <QContextMenuEvent>
#include <QImage>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPushButton>
#include <QResizeEvent>
#include <QString>
#include <QWidget>

#include "control/controlobject.h"
#include "control/controlproxy.h"
#include <QContextMenuEvent>
#include <QEvent>
#include <QImage>

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
protected:
    void enterEvent(QEvent* event) override;
    void leaveEvent(QEvent* event) override;
  public:
    enum class TransitionMode {
        Mix = 0,
        WipeHorizontal = 1,
        WipeVertical = 2,
        Cut = 3,
        Zoom = 4
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

  protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    
  private:
    void buildQuickToolbar();
    void syncToolbarStates();
    void updateToolbarGeometry();
    void drawCentered(QPainter& painter, const QRect& targetRect, const QImage& img, double opacity);
    void renderCameraLayer(QPainter& painter, const QRect& targetRect);
    void renderOverlayLayer(QPainter& painter, const QRect& targetRect);
    void renderLowerThirdLayer(QPainter& painter, const QRect& targetRect);
    void renderRgbSplashLayer(QPainter& painter, const QRect& targetRect);

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
    int m_cameraSource{0};

    bool m_lowerThirdEnabled{false};
    QString m_lowerThirdTitle{QStringLiteral("PHOENIX LIVE")};
    QString m_lowerThirdSubtitle{QStringLiteral("Mixxx 2.4 Video Backport")};

    bool m_overlayEnabled{false};
    int m_overlayStyle{0};

    bool m_rgbSplashEnabled{false};
    QString m_rgbSplashText{QStringLiteral("PHOENIX VJ")};
    int m_rgbPhase{0};

    bool m_toolbarPinned{true};

    bool m_autoFadeActive{false};
    double m_autoFadeProgress{0.0};
    double m_autoFadeStart{0.0};
    double m_autoFadeTarget{1.0};

    QWidget* m_pToolbarContainer{nullptr};
    QComboBox* m_pTransitionDropdown{nullptr};
    QPushButton* m_pBtnFadeNow{nullptr};
    QPushButton* m_pBtnProjector{nullptr};
    QPushButton* m_pBtnCamDropdown{nullptr};
    QPushButton* m_pBtnLowerThird{nullptr};
    QPushButton* m_pBtnOverlay{nullptr};
    QPushButton* m_pBtnRgbSplash{nullptr};
};
// Normalized positions (0.0 = top/left, 1.0 = bottom/right)
QPointF m_lowerThirdPos = QPointF(0.05, 0.80); // Bottom-left default
QPointF m_overlayTextPos = QPointF(0.50, 0.15); // Top-center default
QPointF m_cameraPipPos  = QPointF(0.72, 0.05); // Top-right PiP default
QSizeF  m_cameraPipSize = QSizeF(0.25, 0.25);  // 25% of canvas width/height

enum class DragTarget { None, LowerThird, OverlayText, CameraPip };
DragTarget m_dragTarget = DragTarget::None;
QPointF m_dragOffset;
};
