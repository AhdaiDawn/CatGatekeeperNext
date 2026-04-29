#ifndef CAT_GATEKEEPER_PROCESSED_ASSETS_H
#define CAT_GATEKEEPER_PROCESSED_ASSETS_H

#include <QString>

enum class AssetFormat {
    PngSequence,
    VideoAlpha,
};

struct ClipInfo {
    QString name;
    QString dir;
    QString video;
    int frames = 0;
    bool loop = false;
    int frameWidth = 0;
    int frameHeight = 0;
    int offsetX = 0;
    int offsetY = 0;

    QString framePath(const QString &assetsDir, int frameNumber) const;
    QString videoPath(const QString &assetsDir) const;
    quint64 decodedBytes() const;
};

struct ProcessedAssets {
    QString assetsDir;
    AssetFormat format = AssetFormat::PngSequence;
    int fps = 0;
    int width = 0;
    int height = 0;
    qreal globalOpacity = 1.0;
    qreal backgroundOpacity = 0.38;
    ClipInfo clip1;
    ClipInfo clip2;

    static bool loadBundled(ProcessedAssets *assets, QString *error);
};

#endif
