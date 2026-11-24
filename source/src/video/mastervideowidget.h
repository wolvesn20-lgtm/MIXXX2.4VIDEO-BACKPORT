#pragma once

#include <QContextMenuEvent>
#include <QImage>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
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

    void renderComposite(QPainter& painter, const QRect& targetRect);

  public slots:
    void toggleProjectorWindow();
    void setTransitionMode(TransitionMode mode);

  protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;

  private:
    void drawCentered(QPainter& painter, const QRect& targetRect, const QImage& img, double opacity);

    ControlProxy* m_pCrossfader{nullptr};
    ControlProxy* m_pVol1{nullptr};
    ControlProxy* m_pVol2{nullptr};
    ControlObject* m_pTransitionControl{nullptr};
    ControlObject* m_pUpfaderPreviewControl{nullptr};

    TransitionMode m_transition{TransitionMode::Mix};
    ProjectorWindow* m_pProjectorWindow{nullptr};
};
