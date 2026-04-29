#ifndef CAT_GATEKEEPER_OVERLAY_WINDOW_H
#define CAT_GATEKEEPER_OVERLAY_WINDOW_H

#include "FrameSequence.h"
#include "ProcessedAssets.h"

#include <QElapsedTimer>
#include <QPointF>
#include <QSizeF>
#include <QString>
#include <QTimer>
#include <QWidget>

class QPainter;
class QWindow;

enum class OverlayBackend {
    Auto,
    LayerShell,
    Window,
};

class OverlayWindow : public QWidget {
public:
    OverlayWindow(const ProcessedAssets &assets, int sleepSeconds, QScreen *screen, OverlayBackend backend, QWidget *parent = nullptr);

    bool initialize(QString *error);
    void showOverlay();

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    OverlayBackend selectedBackend() const;
    bool initializeLayerShell(QWindow *window, QString *error);
    QString countdownText(qint64 elapsedMs) const;
    void drawCountdown(QPainter *painter, const QPointF &canvasTopLeft, const QSizeF &drawnCanvas, qreal scale) const;

    ProcessedAssets m_assets;
    FrameSequence m_sequence;
    QElapsedTimer m_elapsed;
    QTimer m_timer;
    QScreen *m_screen = nullptr;
    int m_sleepSeconds = 0;
    OverlayBackend m_backend = OverlayBackend::Auto;
    OverlayBackend m_activeBackend = OverlayBackend::Auto;
};

#endif
