#pragma once

#include <QDragEnterEvent>
#include <QDropEvent>
#include <QImage>
#include <QMap>
#include <QMoveEvent>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QString>
#include <QWidget>

#include "track/track_decl.h"
#include "video/videodecoder.h"

class MasterVideoWidget;

class VideoWidget : public QWidget {
    Q_OBJECT
  public:
    VideoWidget(const QString& group, QWidget* parent = nullptr);
    ~VideoWidget() override;

    void setDecoder(VideoDecoder* decoder);

    static VideoWidget* getDeckWidget(const QString& group);
    static bool isUpfaderPreviewEnabled();
    static void setUpfaderPreviewEnabled(bool enabled);

    const QImage& currentFrame() const { return m_uiFrame; }
    VideoDecoder* decoder() const { return m_pDecoder; }

  public slots:
    void slotFrameReady();
    void slotLoadTrack(TrackPointer pTrack);

  protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void moveEvent(QMoveEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

  private:
    static void syncMasterPreviewGeometry();

    static QMap<QString, VideoWidget*> s_deckWidgets;
    static MasterVideoWidget* s_pMasterWidget;
    static bool s_upfaderPreview;

    VideoDecoder* m_pDecoder{nullptr};
    QImage m_uiFrame;
    uint64_t m_lastRenderedFrame{0};
    QString m_group;
};
