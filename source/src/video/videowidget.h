#pragma once

#include <QImage>
#include <QWidget>

#include "track/track_decl.h"

class VideoDecoder;

class VideoWidget : public QWidget {
    Q_OBJECT
  public:
    explicit VideoWidget(const QString& group, QWidget* parent = nullptr);
    ~VideoWidget() override;

  public slots:
    void slotLoadTrack(TrackPointer track);

  protected:
    void paintEvent(QPaintEvent* event) override;

  private slots:
    void slotFrameDecoded(const QImage& frame);

  private:
    QString m_group;
    VideoDecoder* m_decoder{nullptr};
    QImage m_frame;
};
