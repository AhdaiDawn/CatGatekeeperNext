#include "OverlayWindow.h"
#include "ProcessedAssets.h"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QScreen>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QApplication::setQuitOnLastWindowClosed(true);

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Cat Gatekeeper overlay"));
    parser.addHelpOption();

    QCommandLineOption sleepSecondsOption(QStringLiteral("sleep-seconds"), QStringLiteral("Sleep loop duration in seconds."), QStringLiteral("seconds"));
    QCommandLineOption screenOption(QStringLiteral("screen"), QStringLiteral("Target screen. Only 'primary' is supported."), QStringLiteral("screen"));
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

    if (!sleepOk || sleepSeconds < 1 || sleepSeconds > 3600 || screen != QLatin1String("primary")) {
        qCritical("usage: cat-gatekeeper-overlay --sleep-seconds <1..3600> --screen primary");
        return 2;
    }

    ProcessedAssets assets;
    QString error;
    if (!ProcessedAssets::loadBundled(&assets, &error)) {
        qCritical("%s", qPrintable(error));
        return 3;
    }

    QScreen *primary = QApplication::primaryScreen();
    OverlayWindow window(assets, sleepSeconds, primary);
    if (!window.initialize(&error)) {
        qCritical("%s", qPrintable(error));
        return 4;
    }

    window.showFullScreen();
    return QApplication::exec();
}
