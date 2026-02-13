#ifndef FRAMEBUFFER_H
#define FRAMEBUFFER_H

#include <QObject>
#include <QImage>
#include <QMutex>
#include <QWaitCondition>
#include <deque>
#include <atomic>

namespace MCM {

/**
 * @brief Thread-safe circular frame buffer with maintenance threshold
 * 
 * This buffer ensures smooth playback by maintaining a minimum number
 * of frames before allowing consumption. When buffer drops below the
 * maintenance threshold, playback should pause until buffer recovers.
 * 
 * Uses std::deque as a FIFO queue.
 */
class FrameBuffer : public QObject {
    Q_OBJECT

public:
    explicit FrameBuffer(int maxSize = 30, int minMaintenance = 10, QObject* parent = nullptr);
    ~FrameBuffer();

    /**
     * @brief Push a frame into the buffer (producer side)
     * @param frame The frame to add
     * @return true if frame was added, false if buffer was stopped
     * 
     * If buffer is full, oldest frame is dropped (circular behavior)
     */
    bool push(const QImage& frame);

    /**
     * @brief Pop a frame from the buffer (consumer side)
     * @param timeout Maximum time to wait in milliseconds (-1 = infinite)
     * @return The frame, or null QImage if timeout or stopped
     */
    QImage pop(int timeout = -1);

    /**
     * @brief Try to pop without blocking
     * @return The frame if available, null QImage otherwise
     */
    QImage tryPop();

    /**
     * @brief Get current buffer size
     */
    int size() const;

    /**
     * @brief Check if buffer is empty
     */
    bool isEmpty() const;

    /**
     * @brief Check if buffer is below maintenance threshold
     * @return true if playback should pause
     */
    bool isBelowMaintenance() const;

    /**
     * @brief Check if buffer is healthy (at or above maintenance)
     */
    bool isHealthy() const;

    /**
     * @brief Clear all frames from buffer
     */
    void clear();

    /**
     * @brief Stop the buffer (unblocks any waiting consumers)
     */
    void stop();

    /**
     * @brief Reset and restart the buffer
     */
    void reset();

    /**
     * @brief Update buffer configuration
     */
    void setMaxSize(int maxSize);
    void setMinMaintenance(int minMaintenance);

    int maxSize() const { return m_maxSize; }
    int minMaintenance() const { return m_minMaintenance; }

signals:
    /**
     * @brief Emitted when buffer health state changes
     * @param isHealthy true if buffer is at or above maintenance threshold
     */
    void healthChanged(bool isHealthy);

    /**
     * @brief Emitted when buffer size changes significantly
     * @param currentSize Current number of frames in buffer
     */
    void sizeChanged(int currentSize);

private:
    void checkHealthChange(int currentSize);
    
    std::deque<QImage> m_buffer;  // FIFO queue
    mutable QMutex m_mutex;
    QWaitCondition m_notEmpty;
    
    int m_maxSize;
    int m_minMaintenance;
    std::atomic<bool> m_stopped{false};
    bool m_wasHealthy{false};
};

} // namespace MCM

#endif // FRAMEBUFFER_H
