#pragma once

#include <QImage>
#include <QMutex>
#include <QThread>
#include <QString>

// Forward declaration tells the compiler the class exists without 
// heavy header pollution, keeping compilation times blazing fast.
class ControlProxy;

class VideoDecoder : public QThread {
    Q_OBJECT
public:
    explicit VideoDecoder(const QString& group, QObject* parent = nullptr); 
    ~VideoDecoder() override;

    void openFile(const QString& path);
    void close();

signals:
    void frameDecoded(const QImage& image);
    void openFailed(const QString& reason);

protected:
    void run() override;

private:
    QString m_path;
    QMutex m_mutex;
    bool m_abort{false};
    
    QString m_group;
    
    // Mixxx Core Control Engine Links (Initialized using 2.4 ConfigKeys)
    ControlProxy* m_pPlayPosition{nullptr};
    ControlProxy* m_pPlay{nullptr}; // Added to handle track Play/Pause status safely
};
