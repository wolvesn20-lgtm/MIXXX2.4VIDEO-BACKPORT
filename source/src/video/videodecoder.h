#pragma once

#include <QWidget>
#include "track/track.h"

class VideoDecoder;

class VideoWidget : public QWidget {
    Q_OBJECT
public:
    VideoWidget(const QString& group, QWidget* parent = nullptr);
    ~VideoWidget() override;

public slots:
    void slotLoadTrack(TrackPointer track);
    void slotFrameReady(); // <-- Must match the new signal

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QString m_group;
    VideoDecoder* m_decoder;
};
