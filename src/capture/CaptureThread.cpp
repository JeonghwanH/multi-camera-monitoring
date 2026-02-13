#include "CaptureThread.h"
#include "core/FrameBuffer.h"
#include "core/VideoRecorder.h"
#include <QDebug>

namespace MCM {

CaptureThread::CaptureThread(int slotId, QObject* parent)
    : QThread(parent)
    , m_slotId(slotId)
    , m_sourceType(SourceType::None)
{
}

CaptureThread::~CaptureThread() {
    stopCapture();
}

void CaptureThread::setSource(SourceType type, const QString& source) {
    m_sourceType = type;
    m_source = source;
}

void CaptureThread::setFrameBuffer(FrameBuffer* buffer) {
    m_buffer = buffer;
}

void CaptureThread::setVideoRecorder(VideoRecorder* recorder) {
    m_recorder = recorder;
}

void CaptureThread::stopCapture() {
    m_running = false;
    
    if (isRunning()) {
        // Non-blocking: request stop and wait briefly
        // Thread will exit on its own, don't block main thread
        if (!wait(100)) {  // Only wait 100ms max
            qDebug() << "CaptureThread" << m_slotId << "stopping in background";
            // Thread will clean up on its own - don't force terminate
            // Let it finish naturally to avoid resource leaks
        }
    }
    
    m_connected = false;
}

void CaptureThread::processFrame(const QImage& frame) {
    if (frame.isNull()) {
        return;
    }
    
    // Push to buffer if available
    if (m_buffer) {
        m_buffer->push(frame);
    }
    
    // Write to recorder if recording
    if (m_recorder && m_recorder->isRecording()) {
        m_recorder->writeFrame(frame);
    }
    
    // Emit signal for direct consumers
    emit frameReady(frame);
}

} // namespace MCM

