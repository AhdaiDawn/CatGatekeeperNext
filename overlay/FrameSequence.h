#ifndef CAT_GATEKEEPER_FRAME_SEQUENCE_H
#define CAT_GATEKEEPER_FRAME_SEQUENCE_H

#include "ProcessedAssets.h"
#include "VideoFrameDecoder.h"

#include <QImage>
#include <QVector>

#include <memory>

class FrameSequence {
public:
    explicit FrameSequence(ProcessedAssets assets, int sleepSeconds);

    bool prepare(QString *error);
    bool update(qint64 elapsedMs, QString *error);

    const QImage &currentImage() const;
    const ClipInfo &currentClip() const;
    bool finished() const;

private:
    bool ensureIntroVideoDecoder(QString *error);
    bool ensureSleepVideoDecoder(QString *error);
    bool loadImage(const QString &path, QImage *image, QString *error) const;
    bool loadIntroFrame(int frameNumber, QString *error);
    bool loadSleepFrame(int frameNumber, QString *error);
    bool loadVideoFrame(VideoFrameDecoder *decoder, const ClipInfo &clip, int frameNumber, QString *error);

    ProcessedAssets m_assets;
    int m_sleepSeconds = 0;
    QVector<QImage> m_sleepFrames;
    QImage m_currentImage;
    std::unique_ptr<VideoFrameDecoder> m_introVideoDecoder;
    std::unique_ptr<VideoFrameDecoder> m_sleepVideoDecoder;
    const ClipInfo *m_currentClip = nullptr;
    int m_lastIntroFrame = -1;
    int m_lastSleepFrame = -1;
    bool m_cacheSleepFrames = false;
    bool m_finished = false;
};

#endif
