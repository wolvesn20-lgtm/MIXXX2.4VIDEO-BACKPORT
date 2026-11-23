#pragma once

#include <QImage>
#include <QMap>
#include <QPaintEvent>
#include <QString>
#include <QWidget>

#include "track/track_decl.h"
#include "video/videodecoder.h"

class VideoWidget : public QWidget {
    Q_OBJECT
  public:
    VideoWidget(const QString& group, QWidget* parent = nullptr);
    ~VideoWidget() override; // NEW: unregister on destroy

    void setDecoder(VideoDecoder* decoder);

    // NEW: Accessors for MasterVideoWidget
    static VideoWidget* getDeckWidget(const QString& group);
    const QImage& currentFrame() const { return m_uiFrame; }
    VideoDecoder* decoder() const { return m_pDecoder; }

  public slots:
    void slotFrameReady();
    void slotLoadTrack(TrackPointer pTrack);

  protected:
    void paintEvent(QPaintEvent* event) override;

  private:
    static QMap<QString, VideoWidget*> s_deckWidgets; // NEW

    VideoDecoder* m_pDecoder{nullptr};
    QImage m_uiFrame;
    uint64_t m_lastRenderedFrame{0};
    QString m_group;
};
