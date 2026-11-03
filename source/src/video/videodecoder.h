#pragma once

#include <QImage>
#include <QMutex>
#include <QThread>
#include "control/controlproxy.h" // <-- NEW: For the audio clock

class VideoDecoder : public QThread {
    Q_OBJECT
public:
    // CHANGED: Now accepts the group (e.g., "[Channel1]")
    explicit VideoDecoder(const QString& group, QObject* parent = nullptr); 
    ~VideoDecoder() override;

    void openFile(const QString& path);
    void close();

signals:
    void frameDecoded(const QImage& frame);
    void openFailed(const QString& reason);

protected:
    void run() override;

private:
    QString m_path;
    QMutex m_mutex;
    bool m_abort{false};
    
    // NEW: Variables to track the deck and sync clock
    QString m_group;
    ControlProxy* m_pAudioClock{nullptr};
};
