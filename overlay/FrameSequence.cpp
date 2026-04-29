#include "FrameSequence.h"

#include <QImageReader>

FrameSequence::FrameSequence(ProcessedAssets assets, int sleepSeconds)
    : m_assets(std::move(assets))
    , m_sleepSeconds(sleepSeconds)
{
}

bool FrameSequence::prepare(QString *error)
{
    if (m_assets.format == AssetFormat::PngSequence) {
        constexpr quint64 cacheBudget = 256ULL * 1024ULL * 1024ULL;
        m_cacheSleepFrames = m_assets.clip2.decodedBytes() <= cacheBudget;
        if (m_cacheSleepFrames) {
            m_sleepFrames.reserve(m_assets.clip2.frames);
            for (int i = 1; i <= m_assets.clip2.frames; i++) {
                QImage frame;
                if (!loadImage(m_assets.clip2.framePath(m_assets.assetsDir, i), &frame, error)) {
                    return false;
                }
                m_sleepFrames.push_back(frame);
            }
        }
    }

    return loadIntroFrame(1, error);
}

bool FrameSequence::ensureIntroVideoDecoder(QString *error)
{
    if (m_introVideoDecoder != nullptr) {
        return true;
    }

    m_introVideoDecoder = std::make_unique<VideoFrameDecoder>();
    if (!m_introVideoDecoder->open(m_assets.clip1.videoPath(m_assets.assetsDir), m_assets.clip1.frameWidth, m_assets.clip1.frameHeight, error)) {
        m_introVideoDecoder.reset();
        return false;
    }
    return true;
}

bool FrameSequence::ensureSleepVideoDecoder(QString *error)
{
    if (m_sleepVideoDecoder != nullptr) {
        return true;
    }

    m_introVideoDecoder.reset();
    m_sleepVideoDecoder = std::make_unique<VideoFrameDecoder>();
    if (!m_sleepVideoDecoder->open(m_assets.clip2.videoPath(m_assets.assetsDir), m_assets.clip2.frameWidth, m_assets.clip2.frameHeight, error)) {
        m_sleepVideoDecoder.reset();
        return false;
    }
    return true;
}

bool FrameSequence::loadImage(const QString &path, QImage *image, QString *error) const
{
    QImageReader reader(path);
    reader.setAutoTransform(true);
    QImage loaded = reader.read();
    if (loaded.isNull()) {
        *error = QStringLiteral("cannot decode %1: %2").arg(path, reader.errorString());
        return false;
    }
    *image = loaded.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    return true;
}

bool FrameSequence::loadIntroFrame(int frameNumber, QString *error)
{
    if (frameNumber == m_lastIntroFrame) {
        return true;
    }
    if (m_assets.format == AssetFormat::PngSequence) {
        if (!loadImage(m_assets.clip1.framePath(m_assets.assetsDir, frameNumber), &m_currentImage, error)) {
            return false;
        }
    } else {
        if (!ensureIntroVideoDecoder(error) || !loadVideoFrame(m_introVideoDecoder.get(), m_assets.clip1, frameNumber, error)) {
            return false;
        }
    }
    m_currentClip = &m_assets.clip1;
    m_lastIntroFrame = frameNumber;
    return true;
}

bool FrameSequence::loadSleepFrame(int frameNumber, QString *error)
{
    if (frameNumber == m_lastSleepFrame) {
        return true;
    }
    if (m_assets.format == AssetFormat::PngSequence) {
        if (!loadImage(m_assets.clip2.framePath(m_assets.assetsDir, frameNumber), &m_currentImage, error)) {
            return false;
        }
    } else {
        if (!ensureSleepVideoDecoder(error) || !loadVideoFrame(m_sleepVideoDecoder.get(), m_assets.clip2, frameNumber, error)) {
            return false;
        }
    }
    m_currentClip = &m_assets.clip2;
    m_lastSleepFrame = frameNumber;
    return true;
}

bool FrameSequence::loadVideoFrame(VideoFrameDecoder *decoder, const ClipInfo &clip, int frameNumber, QString *error)
{
    if (decoder == nullptr) {
        *error = QStringLiteral("video decoder is not initialized for %1").arg(clip.name);
        return false;
    }
    return decoder->readFrame(frameNumber, &m_currentImage, error);
}

bool FrameSequence::update(qint64 elapsedMs, QString *error)
{
    if (m_finished) {
        return true;
    }

    qint64 introFrame = elapsedMs * m_assets.fps / 1000 + 1;
    if (introFrame <= m_assets.clip1.frames) {
        return loadIntroFrame(static_cast<int>(introFrame), error);
    }

    qint64 introDurationMs = static_cast<qint64>(m_assets.clip1.frames) * 1000 / m_assets.fps;
    qint64 sleepElapsedMs = elapsedMs - introDurationMs;
    if (sleepElapsedMs >= static_cast<qint64>(m_sleepSeconds) * 1000) {
        m_finished = true;
        return true;
    }

    qint64 sleepFrame = sleepElapsedMs * m_assets.fps / 1000;
    int index = static_cast<int>(sleepFrame % m_assets.clip2.frames);
    if (m_cacheSleepFrames) {
        m_currentImage = m_sleepFrames.at(index);
        m_currentClip = &m_assets.clip2;
        return true;
    }

    return loadSleepFrame(index + 1, error);
}

const QImage &FrameSequence::currentImage() const
{
    return m_currentImage;
}

const ClipInfo &FrameSequence::currentClip() const
{
    return *m_currentClip;
}

bool FrameSequence::finished() const
{
    return m_finished;
}
