#include "video/videowidget.h"
#include "video/videodecoder.h"

#include <QFileInfo>
#include <QPainter>
#include <QMutexLocker>

#include "mixer/basetrackplayer.h"
#include "track/track.h"

VideoWidget::VideoWidget(const QString& group, QWidget* parent)
        : QWidget(parent),
          m_group(group),
          m_decoder(new VideoDecoder(group, this)) {
    setMinimumSize(160, 90);
    setAutoFillBackground(true);
    connect(m_decoder, &VideoDecoder::frameReady, this, &VideoWidget::slotFrameReady);
}

VideoWidget::~VideoWidget() {
    m_decoder->close();
}

void VideoWidget::slotLoadTrack(TrackPointer track) {
    m_decoder->close();
    if (!track) { update(); return; }

    const QFileInfo audio(track->getLocation());
    const QString base = audio.absolutePath() + "/" + audio.completeBaseName();
    const QStringList extensions = {".mp4", ".mkv", ".mov", ".webm"};
    for (const QString& ext : extensions) {
        const QString candidate = base + ext;
        if (QFileInfo::exists(candidate)) {
            m_decoder->openFile(candidate);
            break;
        }
    }
    update();
}

void VideoWidget::slotFrameReady() {
    update();
}

void VideoWidget::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.fillRect(rect(), Qt::black);

    if (!m_decoder) return;

    QMutexLocker locker(m_decoder->getBufferMutex());
    uchar* data = m_decoder->getPixelData();
    int w = m_decoder->getImageWidth();
    int h = m_decoder->getImageHeight();

    if (data && w > 0 && h > 0) {
        QImage frame(data, w, h, QImage::Format_RGB32);
        if (!frame.isNull()) {
            const QSize target = frame.size().scaled(size(), Qt::KeepAspectRatio);
            const QPoint offset((width() - target.width()) / 2, (height() - target.height()) / 2);
            painter.drawImage(QRect(offset, target), frame);
        }
    }
}
