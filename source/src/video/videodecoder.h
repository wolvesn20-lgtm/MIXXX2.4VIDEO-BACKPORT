#pragma once

#include <QImage>
#include <QMutex>
#include <QThread>

class VideoDecoder : public QThread {
    Q_OBJECT
  public:
    explicit VideoDecoder(QObject* parent = nullptr);
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
};
