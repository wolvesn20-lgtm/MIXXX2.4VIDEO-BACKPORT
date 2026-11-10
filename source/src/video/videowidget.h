#pragma once

#include <QWidget>
#include <QImage>
#include <QPaintEvent>
#include <QString>
#include "video/videodecoder.h"

// Pull Mixxx's official global track definitions and type declarations
#include "track/track_decl.h"

class VideoWidget : public QWidget {
    Q_OBJECT
public:
    explicit VideoWidget(QWidget* parent = nullptr);
    ~VideoWidget() override = default;

    void setDecoder(VideoDecoder* decoder);

public slots:
    void slotFrameReady(); 
    
    // Mixxx's legacy skin parser will now link this seamlessly to the track payload
    void slotLoadTrack(TrackPointer pTrack);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    VideoDecoder* m_pDecoder{nullptr};
    QImage m_uiFrame;                 
    uint64_t m_lastRenderedFrame{0};  
};
