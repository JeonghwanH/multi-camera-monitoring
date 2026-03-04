// Author: SeungJae Lee
// CameraRoiWidget: tooling UI for adjusting per-camera ROI rectangles against the live video feed.

#include "CameraRoiWidget.h"

#include <QEvent>
#include <QGraphicsVideoItem>
#include <QLabel>
#include <QMediaCaptureSession>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

#include "managers/CameraManager.h"
#include "widgets/ZoomableVideoView.h"
#include "widgets/RoiStyleUtils.h"

namespace
{
constexpr qreal kDefaultRoiSize = 512.0;
constexpr int kOverlayMargin = 24;
}

// Internal overlay widget that handles user interaction for moving/resizing the ROI rectangle.
class RoiSelectionOverlay : public QWidget
{
    Q_OBJECT

public:
    explicit RoiSelectionOverlay(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents, false);
        setAttribute(Qt::WA_NoSystemBackground, true);
        setMouseTracking(true);
        setFocusPolicy(Qt::NoFocus);
    }

    QRectF roiRect() const { return m_roiRect; }
    QRectF normalizedRoi() const { return m_normalizedRect; }

    void setFrameDimensions(const QSizeF &baseFrameSize, const QSizeF &displayFrameSize)
    {
        QSizeF base = baseFrameSize;
        if (base.width() <= 0.0 || base.height() <= 0.0)
            base = QSizeF(1920.0, 1080.0);

        QSizeF display = displayFrameSize;
        if (display.width() <= 0.0 || display.height() <= 0.0)
            display = QSizeF(width(), height());

        const bool baseChanged = !qFuzzyCompare(m_baseFrameSize.width(), base.width()) ||
                                 !qFuzzyCompare(m_baseFrameSize.height(), base.height());
        const bool displayChanged = !qFuzzyCompare(m_displayFrameSize.width(), display.width()) ||
                                    !qFuzzyCompare(m_displayFrameSize.height(), display.height());

        if (!baseChanged && !displayChanged)
            return;

        m_baseFrameSize = base;
        m_displayFrameSize = display;

        if (m_hasNormalized && m_normalizedRect.isValid())
        {
            m_roiRect = clampRectToBounds(normalizedToPixel(m_normalizedRect));
        }
        else if (width() > 0 && height() > 0)
        {
            initializeDefaultRoi(display);
        }

        emitStateChanged();
        update();
    }

    void setShowLegend(bool show)
    {
        if (m_showLegend == show)
            return;
        m_showLegend = show;
        update();
    }

signals:
    void roiChanged(const QRectF &roiRect);
    void normalizedRoiChanged(const QRectF &normalizedRect);

public slots:
    void resetRoi()
    {
        m_hasNormalized = false;
        m_normalizedRect = QRectF();
        initializeDefaultRoi(size());
        emitStateChanged();
        update();
    }

    void setNormalizedRoi(const QRectF &normalizedRect)
    {
        const QRectF sanitized = sanitizeNormalized(normalizedRect);
        if (!sanitized.isValid())
            return;

        m_hasNormalized = true;
        m_normalizedRect = sanitized;

        if (width() <= 0 || height() <= 0)
            return;

        const QRectF target = clampRectToBounds(normalizedToPixel(m_normalizedRect));
        if (target == m_roiRect)
            return;

        m_roiRect = target;
        emitStateChanged();
        update();
    }

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QWidget::resizeEvent(event);

        if (width() <= 0 || height() <= 0)
            return;

        m_displayFrameSize = event->size();

        QRectF previous = m_roiRect;
        if (m_hasNormalized)
        {
            m_roiRect = clampRectToBounds(normalizedToPixel(m_normalizedRect));
        }
        else
        {
            initializeDefaultRoi(event->size());
        }

        if (!m_hasNormalized && m_roiRect.isValid())
        {
            m_normalizedRect = pixelToNormalized(m_roiRect);
            m_hasNormalized = m_normalizedRect.isValid();
        }

        if (m_roiRect != previous)
            emitStateChanged();

        update();
    }

    void paintEvent(QPaintEvent *event) override
    {
        QWidget::paintEvent(event);

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);

        QPainterPath shade;
        shade.addRect(rect());
        shade.addRect(m_roiRect);
        shade.setFillRule(Qt::OddEvenFill);
        painter.fillPath(shade, QColor(0, 0, 0, 128));

        painter.setPen(RoiStyleUtils::roiOutlinePen());
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(m_roiRect, 6, 6);

        RoiStyleUtils::drawRoiSections(painter, m_roiRect);

        if (m_showLegend)
        {
            const auto legendItems = RoiStyleUtils::detectionLegendItems();
            if (!legendItems.isEmpty())
                RoiStyleUtils::drawLegend(painter, rect(), legendItems);
        }
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() != Qt::LeftButton)
        {
            QWidget::mousePressEvent(event);
            return;
        }

        if (m_roiRect.contains(event->position()))
        {
            m_dragging = true;
            m_dragOffset = event->position() - m_roiRect.topLeft();
            event->accept();
            return;
        }

        QWidget::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (!m_dragging || !(event->buttons() & Qt::LeftButton))
        {
            QWidget::mouseMoveEvent(event);
            return;
        }

        const QPointF desiredTopLeft = event->position() - m_dragOffset;
        moveRoiTo(QRectF(desiredTopLeft, m_roiRect.size()));
        event->accept();
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton && m_dragging)
        {
            m_dragging = false;
            event->accept();
            return;
        }
        QWidget::mouseReleaseEvent(event);
    }

private:
    void initializeDefaultRoi(const QSizeF &size)
    {
        if (size.width() <= 0 || size.height() <= 0)
            return;

        m_displayFrameSize = size;

        const double baseWidth = m_baseFrameSize.width();
        const double baseHeight = m_baseFrameSize.height();

        const double normalizedWidth = std::clamp(kDefaultRoiSize / baseWidth, 0.0, 1.0);
        const double normalizedHeight = std::clamp(kDefaultRoiSize / baseHeight, 0.0, 1.0);
        const double normalizedX = std::clamp((1.0 - normalizedWidth) * 0.5, 0.0, 1.0 - normalizedWidth);
        const double normalizedY = std::clamp((1.0 - normalizedHeight) * 0.5, 0.0, 1.0 - normalizedHeight);

        m_normalizedRect = sanitizeNormalized(QRectF(normalizedX, normalizedY, normalizedWidth, normalizedHeight));
        m_hasNormalized = m_normalizedRect.isValid();
        m_roiRect = clampRectToBounds(normalizedToPixel(m_normalizedRect));
    }

    QRectF clampRectToBounds(const QRectF &rect) const
    {
        if (!rect.isValid() || width() <= 0 || height() <= 0)
            return QRectF();

        QRectF result = rect;
        result.setWidth(std::clamp(result.width(), 0.0, static_cast<double>(width())));
        result.setHeight(std::clamp(result.height(), 0.0, static_cast<double>(height())));
        const qreal maxX = width() - result.width();
        const qreal maxY = height() - result.height();
        result.moveLeft(std::clamp(result.left(), 0.0, std::max(0.0, maxX)));
        result.moveTop(std::clamp(result.top(), 0.0, std::max(0.0, maxY)));
        return result;
    }

    QRectF normalizedToPixel(const QRectF &normalized) const
    {
        if (m_displayFrameSize.width() <= 0.0 || m_displayFrameSize.height() <= 0.0)
            return QRectF();

        const QRectF sanitized = sanitizeNormalized(normalized);
        if (!sanitized.isValid())
            return QRectF();

        return QRectF(sanitized.x() * m_displayFrameSize.width(),
                      sanitized.y() * m_displayFrameSize.height(),
                      sanitized.width() * m_displayFrameSize.width(),
                      sanitized.height() * m_displayFrameSize.height());
    }

    QRectF pixelToNormalized(const QRectF &pixel) const
    {
        if (m_displayFrameSize.width() <= 0.0 || m_displayFrameSize.height() <= 0.0)
            return QRectF();

        QRectF normalized(pixel.x() / m_displayFrameSize.width(),
                          pixel.y() / m_displayFrameSize.height(),
                          pixel.width() / m_displayFrameSize.width(),
                          pixel.height() / m_displayFrameSize.height());

        return sanitizeNormalized(normalized);
    }

    QRectF sanitizeNormalized(const QRectF &rect) const
    {
        if (!rect.isValid())
            return QRectF();

        QRectF result = rect.normalized();
        result.setWidth(std::clamp(result.width(), 0.0, 1.0));
        result.setHeight(std::clamp(result.height(), 0.0, 1.0));
        result.moveLeft(std::clamp(result.left(), 0.0, 1.0 - result.width()));
        result.moveTop(std::clamp(result.top(), 0.0, 1.0 - result.height()));
        if (result.width() <= 0.0 || result.height() <= 0.0)
            return QRectF();
        return result;
    }

    void moveRoiTo(const QRectF &rect)
    {
        if (rect.isEmpty())
            return;

        const QRectF clamped = clampRectToBounds(rect);
        if (clamped == m_roiRect)
            return;

        m_roiRect = clamped;
        if (width() > 0 && height() > 0)
        {
            m_normalizedRect = pixelToNormalized(m_roiRect);
            m_hasNormalized = m_normalizedRect.isValid();
        }
        emitStateChanged();
        update();
    }

    void emitStateChanged()
    {
        emit roiChanged(m_roiRect);
        if (m_hasNormalized && m_normalizedRect.isValid())
            emit normalizedRoiChanged(m_normalizedRect);
    }

    QSizeF m_baseFrameSize {1920.0, 1080.0};
    QSizeF m_displayFrameSize {1920.0, 1080.0};
    QRectF m_roiRect;
    QRectF m_normalizedRect;
    bool m_hasNormalized = false;
    bool m_dragging = false;
    QPointF m_dragOffset;
    bool m_showLegend = true;
};

CameraRoiWidget::CameraRoiWidget(QWidget *parent)
    : QWidget(parent)
{
    buildUi();
}

void CameraRoiWidget::buildUi()
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(kOverlayMargin, kOverlayMargin, kOverlayMargin, kOverlayMargin);
    layout->setSpacing(0);

    m_videoContainer = new QWidget(this);
    m_videoContainer->setAttribute(Qt::WA_StyledBackground, true);
    m_videoContainer->setStyleSheet(QStringLiteral("background-color: #0B0C0F; border-radius: 16px;"));
    auto *containerLayout = new QVBoxLayout(m_videoContainer);
    containerLayout->setContentsMargins(0, 0, 0, 0);
    containerLayout->setSpacing(0);

    m_videoView = new ZoomableVideoView(m_videoContainer);
    m_videoView->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    containerLayout->addWidget(m_videoView);

    m_videoContainer->installEventFilter(this);
    layout->addWidget(m_videoContainer, 1);

    if (m_videoView)
    {
        m_videoView->installEventFilter(this);
        if (m_videoView->viewport())
            m_videoView->viewport()->installEventFilter(this);
    }

    m_roiOverlay = new RoiSelectionOverlay(m_videoView ? m_videoView->viewport() : m_videoContainer);
    m_roiOverlay->hide();
    m_roiOverlay->raise();
    m_roiOverlay->setShowLegend(m_showLegend);

    m_frozenFrameLabel = new QLabel(m_videoView ? m_videoView->viewport() : m_videoContainer);
    if (m_frozenFrameLabel)
    {
        m_frozenFrameLabel->setScaledContents(true);
        m_frozenFrameLabel->setVisible(false);
        m_frozenFrameLabel->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    }

    connect(m_roiOverlay, &RoiSelectionOverlay::roiChanged, this, &CameraRoiWidget::roiChanged);
    connect(m_roiOverlay, &RoiSelectionOverlay::normalizedRoiChanged, this, [this](const QRectF &normalized) {
        m_normalizedRoi = normalized;
        m_hasNormalizedRoi = normalized.isValid();
        if (m_hasPendingNormalizedRoi)
        {
            auto nearlyEqual = [](double a, double b) {
                return std::abs(a - b) < 1e-6;
            };
            const QRectF pending = m_pendingNormalizedRoi;
            const bool matches =
                (!normalized.isValid() && !pending.isValid()) ||
                (normalized.isValid() &&
                 nearlyEqual(normalized.x(), pending.x()) &&
                 nearlyEqual(normalized.y(), pending.y()) &&
                 nearlyEqual(normalized.width(), pending.width()) &&
                 nearlyEqual(normalized.height(), pending.height()));
            if (matches)
                m_hasPendingNormalizedRoi = false;
        }
        emit normalizedRoiChanged(normalized);
    });

    m_captureSession = std::make_unique<QMediaCaptureSession>();
    if (m_videoView && m_captureSession)
        m_captureSession->setVideoOutput(m_videoView->videoItem());

    if (auto *videoItem = m_videoView ? m_videoView->videoItem() : nullptr)
    {
        connect(videoItem, &QGraphicsVideoItem::nativeSizeChanged, this, [this](const QSizeF &) {
            updateOverlayGeometry();
        });
    }

    m_resizeFreezeTimer = new QTimer(this);
    m_resizeFreezeTimer->setSingleShot(true);
    m_resizeFreezeTimer->setInterval(160);
    connect(m_resizeFreezeTimer, &QTimer::timeout, this, &CameraRoiWidget::endResizeFreeze);

    QMetaObject::invokeMethod(this, [this]() { updateOverlayGeometry(); }, Qt::QueuedConnection);
}

void CameraRoiWidget::setCamera(CameraManager *manager, const QString &cameraId)
{
    endResizeFreeze();

    const bool managerChanged = m_cameraManager != manager;
    const bool idChanged = m_cameraId != cameraId;
    if (!managerChanged && !idChanged)
        return;

    if (m_cameraManager)
        disconnect(m_cameraManager, nullptr, this, nullptr);

    m_cameraManager = manager;
    m_cameraId = cameraId;

    if (idChanged)
    {
        m_hasNormalizedRoi = false;
        m_normalizedRoi = QRectF();
        m_hasPendingNormalizedRoi = false;
        m_pendingNormalizedRoi = QRectF();
    }

    if (!m_cameraManager || m_cameraId.isEmpty())
    {
        applyCameraHandle(nullptr);
        if (m_roiOverlay)
            m_roiOverlay->hide();
        return;
    }

    ensureConnections();
    applyCameraHandle(m_cameraManager->cameraHandle(m_cameraId));
    QMetaObject::invokeMethod(this, [this]() { updateOverlayGeometry(); }, Qt::QueuedConnection);
    if (m_roiOverlay)
        m_roiOverlay->setShowLegend(m_showLegend);
}

void CameraRoiWidget::clearCamera()
{
    endResizeFreeze();

    if (m_captureSession)
        m_captureSession->setCamera(nullptr);
    disconnectCameraSignals();
    applyCameraHandle(nullptr);
    m_cameraId.clear();
    m_hasNormalizedRoi = false;
    m_normalizedRoi = QRectF();
    m_hasPendingNormalizedRoi = false;
    m_pendingNormalizedRoi = QRectF();
    if (m_roiOverlay)
        m_roiOverlay->hide();
    if (m_videoView && m_videoView->videoItem())
        m_videoView->videoItem()->setOpacity(0.0);
    if (m_cameraManager)
        disconnect(m_cameraManager, nullptr, this, nullptr);
    m_cameraManager = nullptr;
}

void CameraRoiWidget::resetRoi()
{
    m_hasNormalizedRoi = false;
    m_normalizedRoi = QRectF();
    m_hasPendingNormalizedRoi = false;
    m_pendingNormalizedRoi = QRectF();
    QMetaObject::invokeMethod(this, [this]() { updateOverlayGeometry(); }, Qt::QueuedConnection);
}

void CameraRoiWidget::applyNormalizedRoi(const QRectF &normalizedRoi)
{
    auto sanitize = [](QRectF rect) -> QRectF {
        if (!rect.isValid())
            return QRectF();
        rect = rect.normalized();
        const auto clamp = [](double value) {
            if (value < 0.0)
                return 0.0;
            if (value > 1.0)
                return 1.0;
            return value;
        };
        const double width = std::clamp(rect.width(), 0.0, 1.0);
        const double height = std::clamp(rect.height(), 0.0, 1.0);
        double x = clamp(rect.x());
        double y = clamp(rect.y());
        if (x + width > 1.0)
            x = 1.0 - width;
        if (y + height > 1.0)
            y = 1.0 - height;
        if (width <= 0.0 || height <= 0.0)
            return QRectF();
        return QRectF(x, y, width, height);
    };

    const QRectF sanitized = sanitize(normalizedRoi);
    m_hasNormalizedRoi = sanitized.isValid();
    m_normalizedRoi = sanitized;

    if (sanitized.isValid())
    {
        m_pendingNormalizedRoi = sanitized;
        m_hasPendingNormalizedRoi = true;
    }
    else
    {
        m_pendingNormalizedRoi = QRectF();
        m_hasPendingNormalizedRoi = false;
    }

    if (m_hasPendingNormalizedRoi && m_roiOverlay && m_roiOverlay->width() > 0 && m_roiOverlay->height() > 0)
    {
        const QRectF pending = m_pendingNormalizedRoi;
        m_hasPendingNormalizedRoi = false;
        m_roiOverlay->setNormalizedRoi(pending);
    }
    QMetaObject::invokeMethod(this, [this]() { updateOverlayGeometry(); }, Qt::QueuedConnection);
}

void CameraRoiWidget::setShowLegend(bool show)
{
    if (m_showLegend == show)
        return;
    m_showLegend = show;
    if (m_roiOverlay)
        m_roiOverlay->setShowLegend(m_showLegend);
}

QRectF CameraRoiWidget::currentNormalizedRoi() const
{
    if (m_hasNormalizedRoi && m_normalizedRoi.isValid())
        return m_normalizedRoi;
    if (m_roiOverlay)
        return m_roiOverlay->normalizedRoi();
    return QRectF();
}

void CameraRoiWidget::disconnectCameraSignals()
{
    if (m_cameraActiveConnection)
    {
        QObject::disconnect(m_cameraActiveConnection);
        m_cameraActiveConnection = QMetaObject::Connection();
    }
}

bool CameraRoiWidget::eventFilter(QObject *watched, QEvent *event)
{
    const bool watchedVideoContainer = watched == m_videoContainer;
    const bool watchedVideoView = (m_videoView && watched == m_videoView);
    const bool watchedViewport = (m_videoView && m_videoView->viewport() && watched == m_videoView->viewport());

    if ((watchedVideoContainer || watchedVideoView || watchedViewport))
    {
        switch (event->type())
        {
        case QEvent::Resize:
        case QEvent::Move:
        case QEvent::Show:
        case QEvent::LayoutRequest:
        case QEvent::UpdateRequest:
            if (event->type() == QEvent::Resize)
            {
                const QSize newSize = (watchedViewport ? m_videoView->viewport()->size()
                                                        : watchedVideoView    ? m_videoView->size()
                                                                              : m_videoContainer->size());
                if (newSize != m_lastViewportSize)
                {
                    m_lastViewportSize = newSize;
                    m_pendingFreezeCapture = true;
                }
            }
            else if (m_resizeFrozen)
            {
                // Allow geometry adjustments while frozen, but do not trigger new capture.
            }

            if (m_pendingFreezeCapture && !m_resizeFrozen && m_resizeFreezeTimer)
            {
                m_resizeFreezeTimer->start();
            }
            QMetaObject::invokeMethod(this, [this]() { updateOverlayGeometry(); }, Qt::QueuedConnection);
            break;
        default:
            break;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void CameraRoiWidget::updateOverlayGeometry()
{
    if (!m_roiOverlay || !m_videoView)
        return;

    if (m_cameraId.isEmpty() || !m_cameraHandle)
    {
        m_roiOverlay->hide();
        return;
    }

    const QRectF videoRect = m_videoView->videoViewportRect();
    if (videoRect.isNull() || !videoRect.isValid() || videoRect.width() <= 0 || videoRect.height() <= 0)
    {
        m_roiOverlay->hide();
        return;
    }

    if (m_roiOverlay->parentWidget() != (m_videoView->viewport() ? m_videoView->viewport() : m_videoContainer))
        m_roiOverlay->setParent(m_videoView->viewport() ? m_videoView->viewport() : m_videoContainer);

    QWidget *videoParent = m_videoView->viewport() ? m_videoView->viewport() : m_videoContainer;
    if (m_frozenFrameLabel && m_frozenFrameLabel->parentWidget() != videoParent)
        m_frozenFrameLabel->setParent(videoParent);

    const QRect target = videoRect.toAlignedRect();
    if (m_lastViewportSize != target.size())
        m_lastViewportSize = target.size();
    if (target.width() <= 0 || target.height() <= 0)
    {
        m_roiOverlay->hide();
        if (m_frozenFrameLabel)
            m_frozenFrameLabel->hide();
        endResizeFreeze();
        return;
    }

    if (m_roiOverlay->geometry() != target)
        m_roiOverlay->setGeometry(target);

    if (!m_roiOverlay->isVisible())
        m_roiOverlay->show();

    m_roiOverlay->raise();

    m_roiOverlay->setFrameDimensions(QSizeF(1920.0, 1080.0), QSizeF(target.size()));

    handleResizeFreeze(target);

    if (m_hasPendingNormalizedRoi && m_roiOverlay->width() > 0 && m_roiOverlay->height() > 0)
    {
        const QRectF pending = m_pendingNormalizedRoi;
        m_hasPendingNormalizedRoi = false;
        m_roiOverlay->setNormalizedRoi(pending);
    }
    else if (m_hasNormalizedRoi && m_normalizedRoi.isValid())
    {
        m_roiOverlay->setNormalizedRoi(m_normalizedRoi);
    }
    else
    {
        applyDefaultRoi(videoRect);
    }
}

void CameraRoiWidget::applyCameraHandle(QCamera *camera)
{
    if (!m_captureSession)
        m_captureSession = std::make_unique<QMediaCaptureSession>();

    disconnectCameraSignals();

    if (m_captureSession)
        m_captureSession->setCamera(nullptr);

    m_cameraHandle = camera;

    if (m_videoView && m_videoView->videoItem())
    {
        m_videoView->updateVideoItemSize(QSize(1920, 1080));
        m_videoView->videoItem()->setOpacity(0.0);
    }

    if (m_captureSession && m_videoView)
    {
        m_captureSession->setVideoOutput(m_videoView->videoItem());
        if (m_cameraHandle)
            m_captureSession->setCamera(m_cameraHandle);
    }

    if (!m_cameraHandle)
        return;

    if (m_cameraHandle->isActive() && m_videoView && m_videoView->videoItem())
        m_videoView->videoItem()->setOpacity(1.0);

    m_cameraActiveConnection = connect(m_cameraHandle, &QCamera::activeChanged, this, [this](bool active) {
        if (!m_videoView || !m_videoView->videoItem())
            return;
        m_videoView->videoItem()->setOpacity(active ? 1.0 : 0.0);
    });

    if (!m_cameraHandle->isActive())
    {
        m_cameraHandle->start();
    }

    QMetaObject::invokeMethod(this, [this]() { updateOverlayGeometry(); }, Qt::QueuedConnection);
}

void CameraRoiWidget::applyDefaultRoi(const QRectF &videoRect)
{
    if (!m_roiOverlay)
        return;

    if (videoRect.width() <= 0.0 || videoRect.height() <= 0.0)
        return;

    const qreal side = std::min<qreal>(512.0, std::min(videoRect.width(), videoRect.height()));
    if (side <= 0.0)
        return;

    const qreal normalizedWidth = side / videoRect.width();
    const qreal normalizedHeight = side / videoRect.height();
    const qreal normalizedX = (1.0 - normalizedWidth) / 2.0;
    const qreal normalizedY = (1.0 - normalizedHeight) / 2.0;
    const QRectF normalized(normalizedX, normalizedY, normalizedWidth, normalizedHeight);

    m_roiOverlay->setNormalizedRoi(normalized);
}

void CameraRoiWidget::handleResizeFreeze(const QRect &targetRect)
{
    if (!m_resizeFreezeTimer || !m_frozenFrameLabel)
        return;

    if (!m_resizeFrozen && m_pendingFreezeCapture)
    {
        beginResizeFreeze(targetRect);
    }
    else if (m_resizeFrozen)
    {
        updateFrozenFrameGeometry(targetRect);
    }

    if (m_resizeFrozen && m_resizeFreezeTimer)
        m_resizeFreezeTimer->start();
}

void CameraRoiWidget::beginResizeFreeze(const QRect &targetRect)
{
    if (m_resizeFrozen)
        return;

    if (!m_videoView || !m_videoView->viewport())
        return;

    QWidget *viewport = m_videoView->viewport();
    const bool overlayVisible = m_roiOverlay && m_roiOverlay->isVisible();
    if (overlayVisible)
        m_roiOverlay->hide();

    QPixmap snapshot = viewport->grab(targetRect);

    if (overlayVisible)
    {
        m_roiOverlay->show();
        m_roiOverlay->raise();
    }

    if (snapshot.isNull())
        return;

    m_frozenFrameLabel->setPixmap(snapshot);
    m_frozenFrameLabel->setGeometry(targetRect);
    m_frozenFrameLabel->show();
    m_frozenFrameLabel->raise();

    if (m_videoView->videoItem())
        m_videoView->videoItem()->setOpacity(0.0);

    m_resizeFrozen = true;
    m_pendingFreezeCapture = false;
    if (m_resizeFreezeTimer)
        m_resizeFreezeTimer->start();
}

void CameraRoiWidget::updateFrozenFrameGeometry(const QRect &targetRect)
{
    if (!m_resizeFrozen || !m_frozenFrameLabel)
        return;

    if (m_frozenFrameLabel->geometry() != targetRect)
        m_frozenFrameLabel->setGeometry(targetRect);

    m_frozenFrameLabel->raise();
    if (m_roiOverlay)
        m_roiOverlay->raise();
}

void CameraRoiWidget::endResizeFreeze()
{
    m_pendingFreezeCapture = false;
    if (!m_resizeFrozen)
        return;

    m_resizeFrozen = false;
    if (m_frozenFrameLabel)
    {
        m_frozenFrameLabel->hide();
        m_frozenFrameLabel->setPixmap(QPixmap());
    }

    if (m_videoView && m_videoView->videoItem())
        m_videoView->videoItem()->setOpacity(1.0);

    if (m_roiOverlay)
        m_roiOverlay->raise();
}

void CameraRoiWidget::ensureConnections()
{
    if (!m_cameraManager)
        return;

    connect(m_cameraManager, &CameraManager::cameraHandleAssigned, this,
            [this](const QString &id, QCamera *camera) {
                if (id == m_cameraId)
                    applyCameraHandle(camera);
            });
    connect(m_cameraManager, &CameraManager::cameraRemoved, this, [this](const QString &id) {
        if (id == m_cameraId)
            applyCameraHandle(nullptr);
    });
}

#include "CameraRoiWidget.moc"
