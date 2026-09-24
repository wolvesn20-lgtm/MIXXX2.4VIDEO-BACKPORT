#include "video/videowidget.h"

#include <QFileInfo>
#include <QPainter>
#include <QRegularExpression>

#include "mixer/basetrackplayer.h"
#include "track/track.h"
#include "video/videodecoder.h"

VideoWidget::VideoWidget(const QString& group, QWidget* parent)
        : QWidget(parent),
          m_group(group),
          m_decoder(new VideoDecoder(this)) {
    setMinimumSize(160, 90);
    setAutoFillBackground(true);
    connect(m_decoder, &VideoDecoder::frameDecoded,
            this, &VideoWidget::slotFrameDecoded);
}

VideoWidget::~VideoWidget() {
    m_decoder->close();
}

void VideoWidget::slotLoadTrack(TrackPointer track) {
    m_decoder->close();
    m_frame = QImage();
    if (!track) {
        update();
        return;
    }

    const QFileInfo audio(track->getLocation());
    const QString base = audio.absolutePath() + "/" + audio.completeBaseName();
    const QStringList extensions = {QStringLiteral(".mp4"), QStringLiteral(".mkv"),
            QStringLiteral(".mov"), QStringLiteral(".webm")};
    for (const QString& extension : extensions) {
        const QString candidate = base + extension;
        if (QFileInfo::exists(candidate)) {
            m_decoder->openFile(candidate);
            break;
        }
    }
    update();
}

void VideoWidget::slotFrameDecoded(const QImage& frame) {
    m_frame = frame;
    update();
}

void VideoWidget::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.fillRect(rect(), Qt::black);
    if (m_frame.isNull()) {
        return;
    }
    const QSize target = m_frame.size().scaled(size(), Qt::KeepAspectRatio);
    const QPoint offset((width() - target.width()) / 2,
            (height() - target.height()) / 2);
    painter.drawImage(QRect(offset, target), m_frame);
}
