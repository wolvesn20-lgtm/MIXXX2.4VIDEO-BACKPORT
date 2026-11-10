#pragma once

#include <QWidget>
#include <QImage>
#include <QPaintEvent>
#include <QString>
#include "video/videodecoder.h"
#include "track/track_decl.h"

class SkinContext;

class VideoWidget : public QWidget {
    Q_OBJECT
public:
    // FIX: Adjusted signature to perfectly match the parser invocation types
    VideoWidget(const QString& group, const SkinContext& context);
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
    QString m_group; // Store the channel deck identity group if needed
};
