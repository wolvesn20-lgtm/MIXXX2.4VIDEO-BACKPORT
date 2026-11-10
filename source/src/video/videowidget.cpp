#pragma once

#include <QWidget>
#include <QImage>
#include <QPaintEvent>
#include <QString>
#include "video/videodecoder.h"
#include "track/track_decl.h"

// Forward declare the configurations/skin contexts Mixxx uses
class QDomNode;
class SkinContext;

class VideoWidget : public QWidget {
    Q_OBJECT
public:
    // FIX: Expose a flexible constructor signature that accepts both a parent and skin parser configuration nodes
    VideoWidget(QWidget* parent, const SkinContext& context);
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
};
