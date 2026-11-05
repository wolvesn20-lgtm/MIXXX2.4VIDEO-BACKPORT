#ifndef VIDEODECODER_H
#define VIDEODECODER_H

#include <QThread>
#include <QMutex>
#include <QImage>
#include <QString>

// Forward declaration of Mixxx's control object class to avoid header pollution
class ControlProxy;

class VideoDecoder : public QThread {
    Q_OBJECT

public:
    explicit VideoDecoder(const QString& group, QObject* parent = nullptr);
    virtual ~VideoDecoder() override;

    // Thread control endpoints
    void openFile(const QString& path);
    void close();

signals:
    // Emitted across the thread boundary when a frame or branding graphic is ready
    void frameDecoded(const QImage& image);
    // Emitted if file tracking or decoding contexts fail to allocate
    void openFailed(const QString& message);

protected:
    // The master execution state machine loop
    void run() override;

private:
    QString m_group;
    QString m_path;
    bool m_abort;
    QMutex m_mutex;

    // Mixxx Core Control Engine Links (Allocated using 2.4 ConfigKeys)
    ControlProxy* m_pPlayPosition; // Tracks the absolute file percentage (0.0 to 1.0)
    ControlProxy* m_pPlay;         // Tracks whether the deck state is playing (1.0) or paused (0.0)
};

#endif // VIDEODECODER_H
