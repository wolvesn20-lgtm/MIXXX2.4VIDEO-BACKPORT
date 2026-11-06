#pragma once

#include <QImage>
#include <QMutex>
#include <QThread>
#include <QString>
#include "control/controlproxy.h"

class VideoDecoder : public QThread {
    Q_OBJECT
public:
    explicit VideoDecoder(const QString& group, QObject* parent = nullptr); 
    ~VideoDecoder() override;

    void openFile(const QString& path);
    void close();

    // Shared buffer and property accessors
    uchar* getPixelData() { return m_pixelData; }
    int getImageWidth() const { return m_width; }
    int getImageHeight() const { return m_height; }
    double getCalculatedOpacity() const { return m_calculatedOpacity; }
    QMutex* getBufferMutex() { return &m_bufferMutex; }

signals:
    void frameReady(); // Lightweight data-free notification ping
    void openFailed(const QString& reason);

protected:
    void run() override;

private:
    QString m_path;
    QMutex m_mutex;
    QMutex m_bufferMutex; // Protects the shared pixel buffer
    bool m_abort{false};
    
    QString m_group;
    ControlProxy* m_pPlayPosition{nullptr};
    ControlProxy* m_pPlay{nullptr};
    ControlProxy* m_pVolume{nullptr};      // Tracks Channel Volume (0.0 to 1.0)
    ControlProxy* m_pCrossfader{nullptr};  // Tracks Master Crossfader (-1.0 to 1.0)

    uchar* m_pixelData{nullptr};
    int m_width{0};
    int m_height{0};
    double m_calculatedOpacity{1.0};       // Live channel opacity value
};
