#pragma once

#include <QWidget>
#include <QImage>
#include <QPaintEvent>
#include <QString>
#include "video/videodecoder.h"

// Forward declare Track class and its smart pointer representation
class Track;
typedef QSharedPointer<Track> TrackPointer;

class VideoWidget : public QWidget {
    Q_OBJECT
public:
    explicit VideoWidget(QWidget* parent = nullptr);
    ~VideoWidget() override = default;

    void setDecoder(VideoDecoder* decoder);

public slots:
    void slotFrameReady(); 
    
    // Explicit inline match mapping track locations to the player backend
    void slotLoadTrack(TrackPointer pTrack);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    VideoDecoder* m_pDecoder{nullptr};
    QImage m_uiFrame;                 
    uint64_t m_lastRenderedFrame{0};  
};
