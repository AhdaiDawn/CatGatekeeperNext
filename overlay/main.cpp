#include "OverlayWindow.h"
#include "ProcessedAssets.h"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QScreen>

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

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QApplication::setQuitOnLastWindowClosed(true);

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Cat Gatekeeper overlay"));
    parser.addHelpOption();

    QCommandLineOption sleepSecondsOption(QStringLiteral("sleep-seconds"), QStringLiteral("Sleep loop duration in seconds."), QStringLiteral("seconds"));
    QCommandLineOption screenOption(QStringLiteral("screen"), QStringLiteral("Target screen index. Defaults to 0."), QStringLiteral("index"), QStringLiteral("0"));
    parser.addOption(sleepSecondsOption);
    parser.addOption(screenOption);

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

    if (!sleepOk || sleepSeconds < 1 || sleepSeconds > 3600 || !screenOk || screenIndex < 0) {
        qCritical("usage: cat-gatekeeper-overlay --sleep-seconds <1..3600> --screen <0..N>");
        return 2;
    }

    ProcessedAssets assets;
    QString error;
    if (!ProcessedAssets::loadBundled(&assets, &error)) {
        qCritical("%s", qPrintable(error));
        return 3;
    }

    QScreen *screenTarget = screenByIndex(screenIndex);
    OverlayWindow window(assets, sleepSeconds, screenTarget);
    if (!window.initialize(&error)) {
        qCritical("%s", qPrintable(error));
        return 4;
    }

    window.showFullScreen();
    return QApplication::exec();
}
