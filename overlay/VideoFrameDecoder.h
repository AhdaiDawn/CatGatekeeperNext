#ifndef CAT_GATEKEEPER_VIDEO_FRAME_DECODER_H
#define CAT_GATEKEEPER_VIDEO_FRAME_DECODER_H

#include <QByteArray>
#include <QImage>
#include <QString>
#include <QVector>

#include <cstdint>

struct AVIOContext;
struct AVCodecContext;
struct AVFormatContext;
struct AVFrame;
struct AVPacket;
struct SwsContext;

class VideoFrameDecoder {
public:
    VideoFrameDecoder();
    ~VideoFrameDecoder();

    VideoFrameDecoder(const VideoFrameDecoder &) = delete;
    VideoFrameDecoder &operator=(const VideoFrameDecoder &) = delete;

    bool open(const QString &path, int frameWidth, int frameHeight, QString *error);
    bool readFrame(int frameNumber, QImage *image, QString *error);
    void close();

private:
    static int readPacket(void *opaque, uint8_t *buffer, int bufferSize);
    static int64_t seek(void *opaque, int64_t offset, int whence);

    bool reopen(QString *error);
    bool rewind(QString *error);
    bool decodeNextFrame(bool compose, QImage *image, QString *error);
    bool convertFrame(QString *error);
    bool prepareConverter(int sourceFormat, QString *error);
    bool composeFrame(QImage *image, QString *error);
    QString ffmpegError(int code) const;

    QString m_path;
    int m_frameWidth = 0;
    int m_frameHeight = 0;
    int m_videoWidth = 0;
    int m_videoHeight = 0;
    int m_videoStream = -1;
    int m_currentFrame = 0;
    bool m_draining = false;

    QByteArray m_inputBytes;
    qsizetype m_inputOffset = 0;
    AVIOContext *m_ioContext = nullptr;
    AVFormatContext *m_formatContext = nullptr;
    AVCodecContext *m_codecContext = nullptr;
    AVFrame *m_frame = nullptr;
    AVFrame *m_bgraFrame = nullptr;
    AVPacket *m_packet = nullptr;
    SwsContext *m_swsContext = nullptr;
    QVector<uchar> m_bgraBuffer;
};

#endif
