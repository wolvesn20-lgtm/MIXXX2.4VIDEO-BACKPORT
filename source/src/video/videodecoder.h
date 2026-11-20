#pragma once

#include <QImage>
#include <QMutex>
#include <QString>
#include <QThread>
#include <atomic>
#include "control/controlproxy.h"

struct SwsContext;
struct AVFrame;

class VideoDecoder : public QThread {
    Q_OBJECT
  public:
    explicit VideoDecoder(const QString& group, QObject* parent = nullptr);
    ~VideoDecoder() override;

    void openFile(const QString& path);
    void close();

    // Thread-safe frame & opacity accessors
    bool copyLatestFrame(QImage& outImage, uint64_t& lastCounter);
    double getCalculatedOpacity() const { return m_calculatedOpacity.load(); }

  signals:
    void frameReady();
    void openFailed(const QString& reason);

  protected:
    void run() override;

  private:
    bool scaleFrameToImage(SwsContext* swsCtx, AVFrame* frame, QImage& targetImage, int width, int height);

    QString m_path;
    QMutex m_mutex;
    QMutex m_bufferMutex;
    std::atomic<bool> m_abort{false};
    std::atomic<bool> m_reloadRequested{false};
    std::atomic<bool> m_framePending{false};

    QString m_group;
    ControlProxy* m_pPlayPosition{nullptr};
    ControlProxy* m_pPlay{nullptr};
    ControlProxy* m_pDuration{nullptr};
    ControlProxy* m_pVolume{nullptr};
    ControlProxy* m_pCrossfader{nullptr};

    QImage m_sharedImage;
    std::atomic<double> m_calculatedOpacity{1.0};
    std::atomic<uint64_t> m_frameCounter{0};
};
