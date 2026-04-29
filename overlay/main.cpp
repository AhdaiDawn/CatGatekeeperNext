#include "OverlayWindow.h"
#include "ProcessedAssets.h"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDateTime>
#include <QMessageLogContext>
#include <QScreen>
#include <QSurfaceFormat>

#include <cstdio>

static const char *messageTypeName(QtMsgType type)
{
    switch (type) {
    case QtDebugMsg:
        return "debug";
    case QtInfoMsg:
        return "info";
    case QtWarningMsg:
        return "warning";
    case QtCriticalMsg:
        return "critical";
    case QtFatalMsg:
        return "fatal";
    }
    return "log";
}

static void overlayMessageHandler(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    Q_UNUSED(context);

    const QByteArray timestamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")).toLocal8Bit();
    const QByteArray text = message.toLocal8Bit();
    std::fprintf(stderr, "%s cat-gatekeeper-overlay[%s]: %s\n", timestamp.constData(), messageTypeName(type), text.constData());
    std::fflush(stderr);
}

static QScreen *screenByIndex(int requestedIndex)
{
    const QList<QScreen *> screens = QApplication::screens();
    if (screens.isEmpty()) {
        return nullptr;
    }

    if (requestedIndex < screens.size()) {
        return screens.at(requestedIndex);
    }

    qWarning("requested screen %d is not available; falling back to screen 0", requestedIndex);
    return screens.at(0);
}

static bool parseBackend(const QString &value, OverlayBackend *backend)
{
    const QString normalized = value.toLower();
    if (normalized == QStringLiteral("auto")) {
        *backend = OverlayBackend::Auto;
        return true;
    }
    if (normalized == QStringLiteral("layer-shell")) {
        *backend = OverlayBackend::LayerShell;
        return true;
    }
    if (normalized == QStringLiteral("window")) {
        *backend = OverlayBackend::Window;
        return true;
    }

    return false;
}

int main(int argc, char **argv)
{
    qInstallMessageHandler(overlayMessageHandler);

    QSurfaceFormat format = QSurfaceFormat::defaultFormat();
    format.setAlphaBufferSize(8);
    QSurfaceFormat::setDefaultFormat(format);

    QApplication app(argc, argv);
    QApplication::setQuitOnLastWindowClosed(true);

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Cat Gatekeeper overlay"));
    parser.addHelpOption();

    QCommandLineOption sleepSecondsOption(QStringLiteral("sleep-seconds"), QStringLiteral("Sleep loop duration in seconds."), QStringLiteral("seconds"));
    QCommandLineOption screenOption(QStringLiteral("screen"), QStringLiteral("Target screen index. Defaults to 0."), QStringLiteral("index"), QStringLiteral("0"));
    QCommandLineOption backendOption(QStringLiteral("backend"), QStringLiteral("Overlay backend: auto, layer-shell, or window. Defaults to auto."), QStringLiteral("backend"), QStringLiteral("auto"));
    parser.addOption(sleepSecondsOption);
    parser.addOption(screenOption);
    parser.addOption(backendOption);

    if (!parser.parse(QCoreApplication::arguments())) {
        qCritical("%s", qPrintable(parser.errorText()));
        return 2;
    }
    if (parser.isSet(QStringLiteral("help"))) {
        parser.showHelp(0);
    }

    QString screen = parser.value(screenOption);
    bool sleepOk = false;
    int sleepSeconds = parser.value(sleepSecondsOption).toInt(&sleepOk);
    bool screenOk = false;
    int screenIndex = screen.toInt(&screenOk);
    OverlayBackend backend = OverlayBackend::Auto;
    bool backendOk = parseBackend(parser.value(backendOption), &backend);

    if (!sleepOk || sleepSeconds < 1 || sleepSeconds > 3600 || !screenOk || screenIndex < 0 || !backendOk) {
        qCritical("usage: cat-gatekeeper-overlay --sleep-seconds <1..3600> --screen <0..N> [--backend auto|layer-shell|window]");
        return 2;
    }

    ProcessedAssets assets;
    QString error;
    if (!ProcessedAssets::loadBundled(&assets, &error)) {
        qCritical("%s", qPrintable(error));
        return 3;
    }

    QScreen *screenTarget = screenByIndex(screenIndex);
    OverlayWindow window(assets, sleepSeconds, screenTarget, backend);
    if (!window.initialize(&error)) {
        qCritical("%s", qPrintable(error));
        return 4;
    }

    window.showOverlay();
    return QApplication::exec();
}
