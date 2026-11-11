#pragma once

#include <QWidget>
#include <QImage>
#include <QPaintEvent>
#include <QString>
#include "video/videodecoder.h"
#include "track/track_decl.h"

class VideoWidget : public QWidget {
    Q_OBJECT
public:
    // Exact signature expected by legacyskinparser.cpp
    VideoWidget(const QString& group, QWidget* parent = nullptr);
    ~VideoWidget() override = default;

    void setDecoder(VideoDecoder* decoder);

public slots:
    void slotFrameReady(); 
    void slotLoadTrack(TrackPointer pTrack);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    VideoDecoder* m_pDecoder{nullptr};
    QImage m_uiFrame;                 
    uint64_t m_lastRenderedFrame{0};  
    QString m_group;
};
