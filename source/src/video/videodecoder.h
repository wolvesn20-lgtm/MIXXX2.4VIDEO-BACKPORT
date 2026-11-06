#pragma once

#include <QWidget>
#include <QImage>
#include <QPaintEvent>
#include "video/videodecoder.h"

class VideoWidget : public QWidget {
    Q_OBJECT
public:
    explicit VideoWidget(QWidget* parent = nullptr);
    void setDecoder(VideoDecoder* decoder);

public slots:
    void slotFrameReady(); // Listens to the lightweight frame ping

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    VideoDecoder* m_pDecoder{nullptr};
    QImage m_uiFrame; // The UI thread's private rendering cache
};
