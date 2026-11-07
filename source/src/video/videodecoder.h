#pragma once

#include <QImage>
#include <QMutex>
#include <QThread>
#include "control/controlproxy.h"

// Forward declaration to prevent circular dependency
class VideoDecoder : public QThread {
    Q_OBJECT
public:
    explicit VideoDecoder(const QString& group, QObject* parent = nullptr); 
    ~VideoDecoder() override;

    void openFile(const QString& path);
    void close();

    // Accessors for the shared buffer
    uchar* getPixelData() { return m_pixelData; }
    int getImageWidth() const { return m_width; }
    int getImageHeight() const { return m_height; }
    QMutex* getBufferMutex() { return &m_bufferMutex; }

signals:
    void frameReady(); 
    void openFailed(const QString& reason);

protected:
    void run() override;

private:
    QString m_path;
    QMutex m_mutex;
    QMutex m_bufferMutex;
    bool m_abort{false};
    
    QString m_group;
    ControlProxy* m_pPlayPosition{nullptr};
    ControlProxy* m_pPlay{nullptr};

    uchar* m_pixelData{nullptr};
    int m_width{0};
    int m_height{0};
};
