#include "ProcessedAssets.h"

#include <QDir>
#include <QFile>
#include <QSet>
#include <QTextStream>

QString ClipInfo::framePath(const QString &assetsDir, int frameNumber) const
{
    return QStringLiteral("%1/processed/%2/frame_%3.png")
        .arg(assetsDir, dir, QStringLiteral("%1").arg(frameNumber, 6, 10, QLatin1Char('0')));
}

QString ClipInfo::videoPath(const QString &assetsDir) const
{
    return QStringLiteral("%1/processed/%2").arg(assetsDir, video);
}

quint64 ClipInfo::decodedBytes() const
{
    return static_cast<quint64>(frames) * static_cast<quint64>(frameWidth) * static_cast<quint64>(frameHeight) * 4ULL;
}

static QString trim(QString value)
{
    return value.trimmed();
}

static bool parseInt(const QString &value, int minValue, int maxValue, int *out)
{
    bool ok = false;
    int parsed = value.toInt(&ok);
    if (!ok || parsed < minValue || parsed > maxValue) {
        return false;
    }
    *out = parsed;
    return true;
}

static bool parseOpacity(const QString &value, qreal *out)
{
    bool ok = false;
    double parsed = value.toDouble(&ok);
    if (!ok || parsed < 0.0 || parsed > 1.0) {
        return false;
    }
    *out = parsed;
    return true;
}

static bool parseBool(const QString &value, bool *out)
{
    if (value == QLatin1String("true")) {
        *out = true;
        return true;
    }
    if (value == QLatin1String("false")) {
        *out = false;
        return true;
    }
    return false;
}

static bool safeRelativeDir(const QString &value)
{
    return !value.isEmpty() && !value.startsWith(QLatin1Char('/')) && !value.contains(QLatin1String("..")) && !value.contains(QLatin1Char('/'));
}

static bool safeRelativeFile(const QString &value)
{
    return !value.isEmpty() && !value.startsWith(QLatin1Char('/')) && !value.contains(QLatin1String("..")) && !value.contains(QLatin1Char('/'));
}

static bool readableFile(const QString &path)
{
    QFile file(path);
    return file.exists() && file.open(QIODevice::ReadOnly);
}

static bool validateFrames(const QString &assetsDir, const ClipInfo &clip, QString *error)
{
    for (int i = 1; i <= clip.frames; i++) {
        QString path = clip.framePath(assetsDir, i);
        if (!readableFile(path)) {
            *error = QStringLiteral("missing frame %1").arg(path);
            return false;
        }
    }
    return true;
}

static bool validateVideo(const QString &assetsDir, const ClipInfo &clip, QString *error)
{
    QString path = clip.videoPath(assetsDir);
    if (!readableFile(path)) {
        *error = QStringLiteral("missing video %1").arg(path);
        return false;
    }
    return true;
}

static bool loadAssets(const QString &assetsDir, ProcessedAssets *assets, QString *error)
{
    QString manifestPath = QDir(assetsDir).filePath(QStringLiteral("processed/manifest.conf"));
    QFile file(manifestPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        *error = QStringLiteral("cannot read %1").arg(manifestPath);
        return false;
    }

    *assets = ProcessedAssets();
    assets->assetsDir = assetsDir;

    QSet<QString> seen;
    QTextStream stream(&file);
    int lineNumber = 0;
    while (!stream.atEnd()) {
        lineNumber++;
        QString line = trim(stream.readLine());
        if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) {
            continue;
        }

        int equals = line.indexOf(QLatin1Char('='));
        if (equals < 0) {
            *error = QStringLiteral("%1:%2 expected key=value").arg(manifestPath).arg(lineNumber);
            return false;
        }

        QString key = trim(line.left(equals));
        QString value = trim(line.mid(equals + 1));
        if (key.isEmpty() || value.isEmpty()) {
            *error = QStringLiteral("%1:%2 key and value must be non-empty").arg(manifestPath).arg(lineNumber);
            return false;
        }
        if (seen.contains(key)) {
            *error = QStringLiteral("%1:%2 duplicate key %3").arg(manifestPath).arg(lineNumber).arg(key);
            return false;
        }
        seen.insert(key);

        if (key == QLatin1String("version")) {
            int version = 0;
            if (!parseInt(value, 1, 2, &version)) {
                *error = QStringLiteral("invalid version");
                return false;
            }
        } else if (key == QLatin1String("asset_format")) {
            if (value == QLatin1String("png_sequence")) {
                assets->format = AssetFormat::PngSequence;
            } else if (value == QLatin1String("video_side_by_side")) {
                assets->format = AssetFormat::VideoSideBySide;
            } else {
                *error = QStringLiteral("invalid asset_format");
                return false;
            }
        } else if (key == QLatin1String("fps")) {
            if (!parseInt(value, 1, 240, &assets->fps)) {
                *error = QStringLiteral("invalid fps");
                return false;
            }
        } else if (key == QLatin1String("width")) {
            if (!parseInt(value, 1, 16384, &assets->width)) {
                *error = QStringLiteral("invalid width");
                return false;
            }
        } else if (key == QLatin1String("height")) {
            if (!parseInt(value, 1, 16384, &assets->height)) {
                *error = QStringLiteral("invalid height");
                return false;
            }
        } else if (key == QLatin1String("global_opacity")) {
            if (!parseOpacity(value, &assets->globalOpacity)) {
                *error = QStringLiteral("invalid global_opacity");
                return false;
            }
        } else if (key == QLatin1String("background_opacity")) {
            if (!parseOpacity(value, &assets->backgroundOpacity)) {
                *error = QStringLiteral("invalid background_opacity");
                return false;
            }
        } else if (key == QLatin1String("clip1_name")) {
            assets->clip1.name = value;
        } else if (key == QLatin1String("clip1_dir")) {
            if (!safeRelativeDir(value)) {
                *error = QStringLiteral("invalid clip1_dir");
                return false;
            }
            assets->clip1.dir = value;
        } else if (key == QLatin1String("clip1_video")) {
            if (!safeRelativeFile(value)) {
                *error = QStringLiteral("invalid clip1_video");
                return false;
            }
            assets->clip1.video = value;
        } else if (key == QLatin1String("clip1_frames")) {
            if (!parseInt(value, 1, 100000, &assets->clip1.frames)) {
                *error = QStringLiteral("invalid clip1_frames");
                return false;
            }
        } else if (key == QLatin1String("clip1_loop")) {
            if (!parseBool(value, &assets->clip1.loop)) {
                *error = QStringLiteral("invalid clip1_loop");
                return false;
            }
        } else if (key == QLatin1String("clip1_frame_width")) {
            if (!parseInt(value, 1, 16384, &assets->clip1.frameWidth)) {
                *error = QStringLiteral("invalid clip1_frame_width");
                return false;
            }
        } else if (key == QLatin1String("clip1_frame_height")) {
            if (!parseInt(value, 1, 16384, &assets->clip1.frameHeight)) {
                *error = QStringLiteral("invalid clip1_frame_height");
                return false;
            }
        } else if (key == QLatin1String("clip1_offset_x")) {
            if (!parseInt(value, -16384, 16384, &assets->clip1.offsetX)) {
                *error = QStringLiteral("invalid clip1_offset_x");
                return false;
            }
        } else if (key == QLatin1String("clip1_offset_y")) {
            if (!parseInt(value, -16384, 16384, &assets->clip1.offsetY)) {
                *error = QStringLiteral("invalid clip1_offset_y");
                return false;
            }
        } else if (key == QLatin1String("clip2_name")) {
            assets->clip2.name = value;
        } else if (key == QLatin1String("clip2_dir")) {
            if (!safeRelativeDir(value)) {
                *error = QStringLiteral("invalid clip2_dir");
                return false;
            }
            assets->clip2.dir = value;
        } else if (key == QLatin1String("clip2_video")) {
            if (!safeRelativeFile(value)) {
                *error = QStringLiteral("invalid clip2_video");
                return false;
            }
            assets->clip2.video = value;
        } else if (key == QLatin1String("clip2_frames")) {
            if (!parseInt(value, 1, 100000, &assets->clip2.frames)) {
                *error = QStringLiteral("invalid clip2_frames");
                return false;
            }
        } else if (key == QLatin1String("clip2_loop")) {
            if (!parseBool(value, &assets->clip2.loop)) {
                *error = QStringLiteral("invalid clip2_loop");
                return false;
            }
        } else if (key == QLatin1String("clip2_frame_width")) {
            if (!parseInt(value, 1, 16384, &assets->clip2.frameWidth)) {
                *error = QStringLiteral("invalid clip2_frame_width");
                return false;
            }
        } else if (key == QLatin1String("clip2_frame_height")) {
            if (!parseInt(value, 1, 16384, &assets->clip2.frameHeight)) {
                *error = QStringLiteral("invalid clip2_frame_height");
                return false;
            }
        } else if (key == QLatin1String("clip2_offset_x")) {
            if (!parseInt(value, -16384, 16384, &assets->clip2.offsetX)) {
                *error = QStringLiteral("invalid clip2_offset_x");
                return false;
            }
        } else if (key == QLatin1String("clip2_offset_y")) {
            if (!parseInt(value, -16384, 16384, &assets->clip2.offsetY)) {
                *error = QStringLiteral("invalid clip2_offset_y");
                return false;
            }
        }
    }

    QStringList required = {
        QStringLiteral("fps"),
        QStringLiteral("width"),
        QStringLiteral("height"),
        QStringLiteral("global_opacity"),
        QStringLiteral("clip1_frames"),
        QStringLiteral("clip1_frame_width"),
        QStringLiteral("clip1_frame_height"),
        QStringLiteral("clip1_offset_x"),
        QStringLiteral("clip1_offset_y"),
        QStringLiteral("clip2_frames"),
        QStringLiteral("clip2_frame_width"),
        QStringLiteral("clip2_frame_height"),
        QStringLiteral("clip2_offset_x"),
        QStringLiteral("clip2_offset_y"),
    };
    if (assets->format == AssetFormat::PngSequence) {
        required << QStringLiteral("clip1_dir") << QStringLiteral("clip2_dir");
    } else {
        required << QStringLiteral("asset_format") << QStringLiteral("clip1_video") << QStringLiteral("clip2_video");
    }
    for (const QString &key : required) {
        if (!seen.contains(key)) {
            *error = QStringLiteral("manifest is missing %1").arg(key);
            return false;
        }
    }

    if (assets->format == AssetFormat::PngSequence) {
        if (!validateFrames(assets->assetsDir, assets->clip1, error) || !validateFrames(assets->assetsDir, assets->clip2, error)) {
            return false;
        }
    } else if (!validateVideo(assets->assetsDir, assets->clip1, error) || !validateVideo(assets->assetsDir, assets->clip2, error)) {
        return false;
    }

    return true;
}

bool ProcessedAssets::loadBundled(ProcessedAssets *assets, QString *error)
{
    return loadAssets(QStringLiteral(":/cat-gatekeeper"), assets, error);
}
