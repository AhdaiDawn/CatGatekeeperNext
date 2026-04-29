#include "OverlayWindow.h"

#ifdef Q_OS_WIN
#include <windows.h>
#else
#include <LayerShellQt/Window>
#endif

#include <QApplication>
#include <QFont>
#include <QFontMetricsF>
#include <QPainter>
#include <QScreen>
#include <Qt>

#ifdef Q_OS_WIN
static bool configureWindowsOverlay(QWindow *window, QString *error)
{
    HWND handle = reinterpret_cast<HWND>(window->winId());
    if (handle == NULL) {
        *error = QStringLiteral("cannot access native window handle");
        return false;
    }

    SetLastError(ERROR_SUCCESS);
    LONG_PTR extendedStyle = GetWindowLongPtrW(handle, GWL_EXSTYLE);
    if (extendedStyle == 0 && GetLastError() != ERROR_SUCCESS) {
        *error = QStringLiteral("cannot read native window style");
        return false;
    }

    extendedStyle |= WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW;
    SetLastError(ERROR_SUCCESS);
    if (SetWindowLongPtrW(handle, GWL_EXSTYLE, extendedStyle) == 0 && GetLastError() != ERROR_SUCCESS) {
        *error = QStringLiteral("cannot update native window style");
        return false;
    }

    if (!SetWindowPos(handle, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_FRAMECHANGED)) {
        *error = QStringLiteral("cannot make overlay topmost");
        return false;
    }

    return true;
}
#endif

OverlayWindow::OverlayWindow(const ProcessedAssets &assets, int sleepSeconds, QScreen *screen, QWidget *parent)
    : QWidget(parent)
    , m_assets(assets)
    , m_sequence(assets, sleepSeconds)
    , m_screen(screen)
    , m_sleepSeconds(sleepSeconds)
{
    setWindowTitle(QStringLiteral("Cat Gatekeeper"));
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool | Qt::WindowTransparentForInput | Qt::WindowDoesNotAcceptFocus);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAttribute(Qt::WA_NoSystemBackground, true);
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    setFocusPolicy(Qt::NoFocus);
}

bool OverlayWindow::initialize(QString *error)
{
    if (m_screen == nullptr) {
        *error = QStringLiteral("screen is not available");
        return false;
    }

    setGeometry(m_screen->geometry());
    winId();
    QWindow *window = windowHandle();
    if (window == nullptr) {
        *error = QStringLiteral("cannot create native window");
        return false;
    }
    window->setScreen(m_screen);

#ifdef Q_OS_WIN
    if (!configureWindowsOverlay(window, error)) {
        return false;
    }
#else
    LayerShellQt::Window *layerWindow = LayerShellQt::Window::get(window);
    if (layerWindow == nullptr) {
        *error = QStringLiteral("cannot create layer-shell window");
        return false;
    }

    layerWindow->setScope(QStringLiteral("cat-gatekeeper"));
    layerWindow->setLayer(LayerShellQt::Window::LayerOverlay);
    LayerShellQt::Window::Anchors anchors;
    anchors |= LayerShellQt::Window::AnchorTop;
    anchors |= LayerShellQt::Window::AnchorBottom;
    anchors |= LayerShellQt::Window::AnchorLeft;
    anchors |= LayerShellQt::Window::AnchorRight;
    layerWindow->setAnchors(anchors);
    layerWindow->setExclusiveZone(0);
    layerWindow->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityNone);
    layerWindow->setActivateOnShow(false);
    layerWindow->setScreen(m_screen);
    layerWindow->setDesiredSize(m_screen->geometry().size());
#endif

    if (!m_sequence.prepare(error)) {
        return false;
    }

    connect(&m_timer, &QTimer::timeout, this, [this]() {
        QString error;
        if (!m_sequence.update(m_elapsed.elapsed(), &error)) {
            qCritical("%s", qPrintable(error));
            QApplication::exit(3);
            return;
        }

        if (m_sequence.finished()) {
            QApplication::quit();
            return;
        }

        update();
    });
    m_timer.setTimerType(Qt::PreciseTimer);
    m_timer.setInterval(16);
    m_elapsed.start();
    m_timer.start();
    return true;
}

void OverlayWindow::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.fillRect(rect(), QColor(0, 0, 0, qRound(m_assets.backgroundOpacity * 255.0)));

    const QImage &image = m_sequence.currentImage();
    if (image.isNull()) {
        return;
    }

    const ClipInfo &clip = m_sequence.currentClip();
    QSizeF canvasSize(m_assets.width, m_assets.height);
    QSizeF windowSize(width(), height());
    qreal scale = qMin(windowSize.width() / canvasSize.width(), windowSize.height() / canvasSize.height());
    QSizeF drawnCanvas(canvasSize.width() * scale, canvasSize.height() * scale);
    QPointF canvasTopLeft((windowSize.width() - drawnCanvas.width()) / 2.0, (windowSize.height() - drawnCanvas.height()) / 2.0);

    QRectF target(
        canvasTopLeft.x() + static_cast<qreal>(clip.offsetX) * scale,
        canvasTopLeft.y() + static_cast<qreal>(clip.offsetY) * scale,
        static_cast<qreal>(clip.frameWidth) * scale,
        static_cast<qreal>(clip.frameHeight) * scale);

    constexpr qint64 introSlideMs = 3000;
    if (clip.name == m_assets.clip1.name && m_elapsed.elapsed() < introSlideMs) {
        qreal progress = static_cast<qreal>(m_elapsed.elapsed()) / static_cast<qreal>(introSlideMs);
        target.translate(drawnCanvas.width() * (1.0 - progress), 0.0);
    }

    drawCountdown(&painter, canvasTopLeft, drawnCanvas, scale);

    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.drawImage(target, image);
}

QString OverlayWindow::countdownText(qint64 elapsedMs) const
{
    qint64 introDurationMs = static_cast<qint64>(m_assets.clip1.frames) * 1000 / m_assets.fps;
    qint64 totalDurationMs = introDurationMs + static_cast<qint64>(m_sleepSeconds) * 1000;
    qint64 remainingMs = totalDurationMs - elapsedMs;
    if (remainingMs < 0) {
        remainingMs = 0;
    }

    int remainingSeconds = static_cast<int>((remainingMs + 999) / 1000);
    int minutes = remainingSeconds / 60;
    int seconds = remainingSeconds % 60;
    return QStringLiteral("%1:%2").arg(minutes).arg(seconds, 2, 10, QLatin1Char('0'));
}

void OverlayWindow::drawCountdown(QPainter *painter, const QPointF &canvasTopLeft, const QSizeF &drawnCanvas, qreal scale) const
{
    if (painter == nullptr || scale <= 0.0) {
        return;
    }

    QFont font(QStringLiteral("Sans Serif"));
    font.setBold(true);
    font.setPixelSize(qMax(1, qRound(160.0 * scale)));
    painter->setFont(font);

    const QString text = countdownText(m_elapsed.elapsed());
    QFontMetricsF metrics(font);
    const qreal paddingX = 60.0 * scale;
    const qreal paddingY = 60.0 * scale;
    const qreal centerX = canvasTopLeft.x() + drawnCanvas.width() * 0.35;
    const qreal top = canvasTopLeft.y() + drawnCanvas.height() * 0.20;
    QRectF backgroundRect(
        centerX - (metrics.horizontalAdvance(text) + paddingX * 2.0) / 2.0,
        top,
        metrics.horizontalAdvance(text) + paddingX * 2.0,
        metrics.height() + paddingY * 2.0);

    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setPen(Qt::NoPen);
    painter->setBrush(QColor(0, 0, 0, 90));
    painter->drawRoundedRect(backgroundRect, 20.0 * scale, 20.0 * scale);

    painter->setPen(Qt::white);
    painter->setBrush(Qt::NoBrush);
    painter->drawText(backgroundRect.adjusted(paddingX, paddingY, -paddingX, -paddingY), Qt::AlignCenter, text);
}
