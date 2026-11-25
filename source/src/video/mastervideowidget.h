#pragma once

#include <QContextMenuEvent>
#include <QImage>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPushButton>
#include <QResizeEvent>
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
};

class MasterVideoWidget : public QWidget {
    Q_OBJECT
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

  protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

  private:
    void drawCentered(QPainter& painter, const QRect& targetRect, const QImage& img, double opacity);
    void updateOverlayLayout();

    ControlProxy* m_pCrossfader{nullptr};
    ControlProxy* m_pVol1{nullptr};
    ControlProxy* m_pVol2{nullptr};
    ControlProxy* m_pPlay1{nullptr};
    ControlProxy* m_pPlay2{nullptr};

    ControlObject* m_pTransitionControl{nullptr};
    ControlObject* m_pUpfaderPreviewControl{nullptr};
    ControlObject* m_pCueTransitionControl{nullptr};
    ControlObject* m_pAutoPlayControl{nullptr};
    ControlObject* m_pRandomTransitionControl{nullptr};

    TransitionMode m_transition{TransitionMode::Mix};
    ProjectorWindow* m_pProjectorWindow{nullptr};

    bool m_cueTransition{false};
    bool m_autoPlayEnabled{true};
    bool m_randomTransition{false};

    bool m_autoFadeActive{false};
    double m_autoFadeProgress{0.0};
    double m_autoFadeStart{0.0};
    double m_autoFadeTarget{1.0};

    QPushButton* m_pCueBtn{nullptr};
    QPushButton* m_pAutoTransBtn{nullptr};
    QPushButton* m_pAutoPlayBtn{nullptr};
};
