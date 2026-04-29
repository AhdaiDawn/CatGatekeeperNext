#include "VideoFrameDecoder.h"

#include <QFile>
#include <QtGlobal>

#include <algorithm>
#include <cstdio>
#include <cstring>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/error.h>
#include <libavutil/imgutils.h>
#include <libavutil/mem.h>
#include <libavutil/pixdesc.h>
#include <libswscale/swscale.h>
}

namespace {
constexpr int kDefaultDecoderThreadCount = 2;
constexpr int kMaxDecoderThreadCount = 8;

int decoderThreadCount()
{
    bool ok = false;
    int value = qEnvironmentVariableIntValue("CAT_GATEKEEPER_VIDEO_THREADS", &ok);
    if (!ok) {
        return kDefaultDecoderThreadCount;
    }
    return std::clamp(value, 1, kMaxDecoderThreadCount);
}
}

VideoFrameDecoder::VideoFrameDecoder() = default;

VideoFrameDecoder::~VideoFrameDecoder()
{
    close();
}

QString VideoFrameDecoder::ffmpegError(int code) const
{
    char buffer[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(code, buffer, sizeof(buffer));
    return QString::fromLocal8Bit(buffer);
}

static bool qtResourcePath(const QString &path)
{
    return path.startsWith(QLatin1String(":/")) || path.startsWith(QLatin1String("qrc:/"));
}

int VideoFrameDecoder::readPacket(void *opaque, uint8_t *buffer, int bufferSize)
{
    auto *decoder = static_cast<VideoFrameDecoder *>(opaque);
    qsizetype remaining = decoder->m_inputBytes.size() - decoder->m_inputOffset;
    if (remaining <= 0) {
        return AVERROR_EOF;
    }

    qsizetype bytesToCopy = std::min<qsizetype>(remaining, bufferSize);
    std::memcpy(buffer, decoder->m_inputBytes.constData() + decoder->m_inputOffset, static_cast<size_t>(bytesToCopy));
    decoder->m_inputOffset += bytesToCopy;
    return static_cast<int>(bytesToCopy);
}

int64_t VideoFrameDecoder::seek(void *opaque, int64_t offset, int whence)
{
    auto *decoder = static_cast<VideoFrameDecoder *>(opaque);
    if (whence == AVSEEK_SIZE) {
        return decoder->m_inputBytes.size();
    }

    int64_t nextOffset = 0;
    switch (whence) {
    case SEEK_SET:
        nextOffset = offset;
        break;
    case SEEK_CUR:
        nextOffset = static_cast<int64_t>(decoder->m_inputOffset) + offset;
        break;
    case SEEK_END:
        nextOffset = static_cast<int64_t>(decoder->m_inputBytes.size()) + offset;
        break;
    default:
        return -1;
    }

    if (nextOffset < 0 || nextOffset > decoder->m_inputBytes.size()) {
        return -1;
    }

    decoder->m_inputOffset = static_cast<qsizetype>(nextOffset);
    return nextOffset;
}

bool VideoFrameDecoder::open(const QString &path, int frameWidth, int frameHeight, QString *error)
{
    close();

    m_path = path;
    m_frameWidth = frameWidth;
    m_frameHeight = frameHeight;
    if (qtResourcePath(path)) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            *error = QStringLiteral("cannot read bundled video %1").arg(path);
            return false;
        }
        m_inputBytes = file.readAll();
        if (m_inputBytes.isEmpty()) {
            *error = QStringLiteral("bundled video %1 is empty").arg(path);
            return false;
        }
    }
    return reopen(error);
}

bool VideoFrameDecoder::reopen(QString *error)
{
    AVFormatContext *formatContext = nullptr;
    int result = 0;
    if (m_inputBytes.isEmpty()) {
        QByteArray pathBytes = m_path.toLocal8Bit();
        result = avformat_open_input(&formatContext, pathBytes.constData(), nullptr, nullptr);
    } else {
        m_inputOffset = 0;
        constexpr int ioBufferSize = 32768;
        unsigned char *ioBuffer = static_cast<unsigned char *>(av_malloc(ioBufferSize));
        if (ioBuffer == nullptr) {
            *error = QStringLiteral("cannot allocate video input buffer for %1").arg(m_path);
            return false;
        }

        m_ioContext = avio_alloc_context(ioBuffer, ioBufferSize, 0, this, &VideoFrameDecoder::readPacket, nullptr, &VideoFrameDecoder::seek);
        if (m_ioContext == nullptr) {
            av_free(ioBuffer);
            *error = QStringLiteral("cannot allocate video IO context for %1").arg(m_path);
            return false;
        }

        formatContext = avformat_alloc_context();
        if (formatContext == nullptr) {
            *error = QStringLiteral("cannot allocate video format context for %1").arg(m_path);
            return false;
        }
        formatContext->pb = m_ioContext;
        formatContext->flags |= AVFMT_FLAG_CUSTOM_IO;
        result = avformat_open_input(&formatContext, nullptr, nullptr, nullptr);
    }
    if (result < 0) {
        *error = QStringLiteral("cannot open video %1: %2").arg(m_path, ffmpegError(result));
        if (formatContext != nullptr) {
            avformat_free_context(formatContext);
        }
        return false;
    }
    m_formatContext = formatContext;

    result = avformat_find_stream_info(m_formatContext, nullptr);
    if (result < 0) {
        *error = QStringLiteral("cannot read video stream info %1: %2").arg(m_path, ffmpegError(result));
        return false;
    }

    result = av_find_best_stream(m_formatContext, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (result < 0) {
        *error = QStringLiteral("video has no decodable stream %1: %2").arg(m_path, ffmpegError(result));
        return false;
    }
    m_videoStream = result;

    AVStream *stream = m_formatContext->streams[m_videoStream];
    const AVCodec *codec = avcodec_find_decoder(stream->codecpar->codec_id);
    if (codec == nullptr) {
        *error = QStringLiteral("cannot find decoder for %1").arg(m_path);
        return false;
    }

    m_codecContext = avcodec_alloc_context3(codec);
    if (m_codecContext == nullptr) {
        *error = QStringLiteral("cannot allocate decoder context for %1").arg(m_path);
        return false;
    }

    result = avcodec_parameters_to_context(m_codecContext, stream->codecpar);
    if (result < 0) {
        *error = QStringLiteral("cannot copy decoder parameters for %1: %2").arg(m_path, ffmpegError(result));
        return false;
    }
    m_codecContext->thread_count = decoderThreadCount();
    m_codecContext->thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE;

    result = avcodec_open2(m_codecContext, codec, nullptr);
    if (result < 0) {
        *error = QStringLiteral("cannot open decoder for %1: %2").arg(m_path, ffmpegError(result));
        return false;
    }

    m_videoWidth = m_codecContext->width;
    m_videoHeight = m_codecContext->height;
    if (m_videoWidth != m_frameWidth * 2 || m_videoHeight != m_frameHeight) {
        *error = QStringLiteral("video %1 must be %2x%3, got %4x%5")
                     .arg(m_path)
                     .arg(m_frameWidth * 2)
                     .arg(m_frameHeight)
                     .arg(m_videoWidth)
                     .arg(m_videoHeight);
        return false;
    }

    m_frame = av_frame_alloc();
    m_colorFrame = av_frame_alloc();
    m_alphaFrame = av_frame_alloc();
    m_packet = av_packet_alloc();
    if (m_frame == nullptr || m_colorFrame == nullptr || m_alphaFrame == nullptr || m_packet == nullptr) {
        *error = QStringLiteral("cannot allocate video frame buffers for %1").arg(m_path);
        return false;
    }

    int colorBufferSize = av_image_get_buffer_size(AV_PIX_FMT_BGRA, m_frameWidth, m_frameHeight, 1);
    int alphaBufferSize = av_image_get_buffer_size(AV_PIX_FMT_GRAY8, m_frameWidth, m_frameHeight, 1);
    if (colorBufferSize <= 0 || alphaBufferSize <= 0) {
        *error = QStringLiteral("cannot size conversion buffers for %1").arg(m_path);
        return false;
    }
    m_colorBuffer.resize(colorBufferSize);
    m_alphaBuffer.resize(alphaBufferSize);
    result = av_image_fill_arrays(m_colorFrame->data, m_colorFrame->linesize, m_colorBuffer.data(), AV_PIX_FMT_BGRA, m_frameWidth, m_frameHeight, 1);
    if (result < 0) {
        *error = QStringLiteral("cannot prepare color buffer for %1: %2").arg(m_path, ffmpegError(result));
        return false;
    }
    result = av_image_fill_arrays(m_alphaFrame->data, m_alphaFrame->linesize, m_alphaBuffer.data(), AV_PIX_FMT_GRAY8, m_frameWidth, m_frameHeight, 1);
    if (result < 0) {
        *error = QStringLiteral("cannot prepare alpha buffer for %1: %2").arg(m_path, ffmpegError(result));
        return false;
    }

    m_colorSwsContext = sws_getContext(
        m_frameWidth,
        m_frameHeight,
        m_codecContext->pix_fmt,
        m_frameWidth,
        m_frameHeight,
        AV_PIX_FMT_BGRA,
        SWS_FAST_BILINEAR,
        nullptr,
        nullptr,
        nullptr);
    m_alphaSwsContext = sws_getContext(
        m_frameWidth,
        m_frameHeight,
        m_codecContext->pix_fmt,
        m_frameWidth,
        m_frameHeight,
        AV_PIX_FMT_GRAY8,
        SWS_FAST_BILINEAR,
        nullptr,
        nullptr,
        nullptr);
    if (m_colorSwsContext == nullptr || m_alphaSwsContext == nullptr) {
        *error = QStringLiteral("cannot create pixel converters for %1").arg(m_path);
        return false;
    }

    m_currentFrame = 0;
    m_draining = false;
    return true;
}

void VideoFrameDecoder::close()
{
    if (m_alphaSwsContext != nullptr) {
        sws_freeContext(m_alphaSwsContext);
        m_alphaSwsContext = nullptr;
    }
    if (m_colorSwsContext != nullptr) {
        sws_freeContext(m_colorSwsContext);
        m_colorSwsContext = nullptr;
    }
    if (m_packet != nullptr) {
        av_packet_free(&m_packet);
    }
    if (m_alphaFrame != nullptr) {
        av_frame_free(&m_alphaFrame);
    }
    if (m_colorFrame != nullptr) {
        av_frame_free(&m_colorFrame);
    }
    if (m_frame != nullptr) {
        av_frame_free(&m_frame);
    }
    if (m_codecContext != nullptr) {
        avcodec_free_context(&m_codecContext);
    }
    if (m_formatContext != nullptr) {
        avformat_close_input(&m_formatContext);
    }
    if (m_ioContext != nullptr) {
        avio_context_free(&m_ioContext);
    }
    m_alphaBuffer.clear();
    m_colorBuffer.clear();
    m_inputBytes.clear();
    m_inputOffset = 0;
    m_videoStream = -1;
    m_currentFrame = 0;
    m_draining = false;
    m_videoWidth = 0;
    m_videoHeight = 0;
}

bool VideoFrameDecoder::rewind(QString *error)
{
    int result = av_seek_frame(m_formatContext, m_videoStream, 0, AVSEEK_FLAG_BACKWARD);
    if (result < 0) {
        QString path = m_path;
        int frameWidth = m_frameWidth;
        int frameHeight = m_frameHeight;
        QByteArray inputBytes = m_inputBytes;
        close();
        m_path = path;
        m_frameWidth = frameWidth;
        m_frameHeight = frameHeight;
        m_inputBytes = inputBytes;
        return reopen(error);
    }

    avcodec_flush_buffers(m_codecContext);
    m_currentFrame = 0;
    m_draining = false;
    return true;
}

bool VideoFrameDecoder::readFrame(int frameNumber, QImage *image, QString *error)
{
    if (frameNumber < 1) {
        *error = QStringLiteral("invalid video frame number %1").arg(frameNumber);
        return false;
    }

    if (frameNumber <= m_currentFrame) {
        if (!rewind(error)) {
            return false;
        }
    }

    while (m_currentFrame < frameNumber) {
        bool compose = (m_currentFrame + 1 == frameNumber);
        if (!decodeNextFrame(compose, image, error)) {
            return false;
        }
    }

    return true;
}

bool VideoFrameDecoder::decodeNextFrame(bool compose, QImage *image, QString *error)
{
    while (true) {
        int result = avcodec_receive_frame(m_codecContext, m_frame);
        if (result == 0) {
            m_currentFrame++;
            if (compose) {
                if (!convertSideBySideFrame(error)) {
                    av_frame_unref(m_frame);
                    return false;
                }
                av_frame_unref(m_frame);
                return composeSideBySideFrame(image, error);
            }
            av_frame_unref(m_frame);
            return true;
        }
        if (result == AVERROR_EOF) {
            *error = QStringLiteral("video decoder reached EOF in %1").arg(m_path);
            return false;
        }
        if (result != AVERROR(EAGAIN)) {
            *error = QStringLiteral("cannot decode video frame for %1: %2").arg(m_path, ffmpegError(result));
            return false;
        }

        while (true) {
            if (m_draining) {
                *error = QStringLiteral("video ended before requested frame in %1").arg(m_path);
                return false;
            }

            result = av_read_frame(m_formatContext, m_packet);
            if (result < 0) {
                result = avcodec_send_packet(m_codecContext, nullptr);
                if (result < 0 && result != AVERROR_EOF) {
                    *error = QStringLiteral("cannot flush video decoder for %1: %2").arg(m_path, ffmpegError(result));
                    return false;
                }
                m_draining = true;
                break;
            }

            if (m_packet->stream_index != m_videoStream) {
                av_packet_unref(m_packet);
                continue;
            }

            result = avcodec_send_packet(m_codecContext, m_packet);
            av_packet_unref(m_packet);
            if (result == AVERROR(EAGAIN)) {
                break;
            }
            if (result < 0) {
                *error = QStringLiteral("cannot send video packet for %1: %2").arg(m_path, ffmpegError(result));
                return false;
            }
            break;
        }
    }
}

bool VideoFrameDecoder::halfFrameData(bool rightHalf, const uchar *data[4], int linesize[4], QString *error) const
{
    for (int i = 0; i < 4; i++) {
        data[i] = m_frame->data[i];
        linesize[i] = m_frame->linesize[i];
    }

    if (!rightHalf) {
        return true;
    }

    const AVPixelFormat format = static_cast<AVPixelFormat>(m_frame->format);
    const AVPixFmtDescriptor *descriptor = av_pix_fmt_desc_get(format);
    if (descriptor == nullptr || (descriptor->flags & AV_PIX_FMT_FLAG_PLANAR) == 0 || descriptor->nb_components < 3) {
        *error = QStringLiteral("unsupported side-by-side pixel format in %1").arg(m_path);
        return false;
    }

    for (int i = 0; i < 3; i++) {
        if (data[i] == nullptr) {
            *error = QStringLiteral("missing video plane in %1").arg(m_path);
            return false;
        }
    }

    const int lumaBytes = (descriptor->comp[0].depth + 7) / 8;
    const int chromaBytes = (descriptor->comp[1].depth + 7) / 8;
    data[0] += static_cast<qsizetype>(m_frameWidth) * lumaBytes;
    data[1] += static_cast<qsizetype>(m_frameWidth >> descriptor->log2_chroma_w) * chromaBytes;
    data[2] += static_cast<qsizetype>(m_frameWidth >> descriptor->log2_chroma_w) * chromaBytes;
    return true;
}

bool VideoFrameDecoder::convertSideBySideFrame(QString *error)
{
    const uchar *colorData[4] = {};
    int colorLinesize[4] = {};
    const uchar *alphaData[4] = {};
    int alphaLinesize[4] = {};
    if (!halfFrameData(false, colorData, colorLinesize, error) || !halfFrameData(true, alphaData, alphaLinesize, error)) {
        return false;
    }

    sws_scale(
        m_colorSwsContext,
        colorData,
        colorLinesize,
        0,
        m_frameHeight,
        m_colorFrame->data,
        m_colorFrame->linesize);
    sws_scale(
        m_alphaSwsContext,
        alphaData,
        alphaLinesize,
        0,
        m_frameHeight,
        m_alphaFrame->data,
        m_alphaFrame->linesize);
    return true;
}

bool VideoFrameDecoder::composeSideBySideFrame(QImage *image, QString *error)
{
    QImage composed(m_frameWidth, m_frameHeight, QImage::Format_ARGB32_Premultiplied);
    if (composed.isNull()) {
        *error = QStringLiteral("cannot allocate composed frame for %1").arg(m_path);
        return false;
    }

    for (int y = 0; y < m_frameHeight; y++) {
        const uchar *color = m_colorFrame->data[0] + y * m_colorFrame->linesize[0];
        const uchar *alpha = m_alphaFrame->data[0] + y * m_alphaFrame->linesize[0];
        uchar *out = composed.scanLine(y);

        for (int x = 0; x < m_frameWidth; x++) {
            const int colorOffset = x * 4;
            int a = alpha[x];
            out[colorOffset + 0] = static_cast<uchar>((color[colorOffset + 0] * a + 127) / 255);
            out[colorOffset + 1] = static_cast<uchar>((color[colorOffset + 1] * a + 127) / 255);
            out[colorOffset + 2] = static_cast<uchar>((color[colorOffset + 2] * a + 127) / 255);
            out[colorOffset + 3] = static_cast<uchar>(a);
        }
    }

    *image = composed;
    return true;
}
