#include "FrameBuffer.h"
#include <QMutexLocker>

namespace MCM {

FrameBuffer::FrameBuffer(int maxSize, int minMaintenance, QObject* parent)
    : QObject(parent)
    , m_maxSize(maxSize)
    , m_minMaintenance(minMaintenance)
{
}

FrameBuffer::~FrameBuffer() {
    stop();
}

bool FrameBuffer::push(const QImage& frame) {
    if (m_stopped) {
        return false;
    }
    
    QMutexLocker locker(&m_mutex);
    
    // Drop oldest frame if buffer is full (circular behavior)
    if (static_cast<int>(m_buffer.size()) >= m_maxSize) {
        m_buffer.pop_front();
    }
    
    m_buffer.push_back(frame);
    
    int currentSize = static_cast<int>(m_buffer.size());
    checkHealthChange(currentSize);
    
    emit sizeChanged(currentSize);
    
    // Wake up any waiting consumer
    m_notEmpty.wakeOne();
    
    return true;
}

QImage FrameBuffer::pop(int timeout) {
    QMutexLocker locker(&m_mutex);
    
    // Wait for data or stop signal
    while (m_buffer.empty() && !m_stopped) {
        if (timeout < 0) {
            m_notEmpty.wait(&m_mutex);
        } else {
            if (!m_notEmpty.wait(&m_mutex, timeout)) {
                return QImage();  // Timeout
            }
        }
    }
    
    if (m_stopped || m_buffer.empty()) {
        return QImage();
    }
    
    QImage frame = m_buffer.front();
    m_buffer.pop_front();
    
    int currentSize = static_cast<int>(m_buffer.size());
    checkHealthChange(currentSize);
    
    emit sizeChanged(currentSize);
    
    return frame;
}

QImage FrameBuffer::tryPop() {
    QMutexLocker locker(&m_mutex);
    
    if (m_buffer.empty()) {
        return QImage();
    }
    
    QImage frame = m_buffer.front();
    m_buffer.pop_front();
    
    int currentSize = static_cast<int>(m_buffer.size());
    checkHealthChange(currentSize);
    
    emit sizeChanged(currentSize);
    
    return frame;
}

void FrameBuffer::checkHealthChange(int currentSize) {
    // Simple threshold: healthy when we have at least 5 frames
    // Once healthy, stay healthy (no buffering interruptions)
    constexpr int STARTUP_THRESHOLD = 5;
    
    if (!m_wasHealthy && currentSize >= STARTUP_THRESHOLD) {
        m_wasHealthy = true;
        emit healthChanged(true);
    }
    // Note: We don't go back to unhealthy - display continues even if buffer drops
}

int FrameBuffer::size() const {
    QMutexLocker locker(&m_mutex);
    return static_cast<int>(m_buffer.size());
}

bool FrameBuffer::isEmpty() const {
    QMutexLocker locker(&m_mutex);
    return m_buffer.empty();
}

bool FrameBuffer::isBelowMaintenance() const {
    QMutexLocker locker(&m_mutex);
    return static_cast<int>(m_buffer.size()) < m_minMaintenance;
}

bool FrameBuffer::isHealthy() const {
    QMutexLocker locker(&m_mutex);
    int currentSize = static_cast<int>(m_buffer.size());
    return m_wasHealthy ? (currentSize >= m_minMaintenance) : (currentSize >= m_maxSize);
}

void FrameBuffer::clear() {
    QMutexLocker locker(&m_mutex);
    m_buffer.clear();
    m_wasHealthy = false;
    emit healthChanged(false);
    emit sizeChanged(0);
}

void FrameBuffer::stop() {
    m_stopped = true;
    m_notEmpty.wakeAll();
}

void FrameBuffer::reset() {
    QMutexLocker locker(&m_mutex);
    m_buffer.clear();
    m_stopped = false;
    m_wasHealthy = false;
}

void FrameBuffer::setMaxSize(int maxSize) {
    QMutexLocker locker(&m_mutex);
    m_maxSize = maxSize;
    
    // Trim buffer if needed
    while (static_cast<int>(m_buffer.size()) > m_maxSize) {
        m_buffer.pop_front();
    }
}

void FrameBuffer::setMinMaintenance(int minMaintenance) {
    QMutexLocker locker(&m_mutex);
    m_minMaintenance = minMaintenance;
    
    int currentSize = static_cast<int>(m_buffer.size());
    checkHealthChange(currentSize);
}

} // namespace MCM
