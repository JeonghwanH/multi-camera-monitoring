#include "MonitoringScreen.h"
#include "CameraSlot.h"
#include "ExpandedView.h"
#include "utils/DeviceDetector.h"
#include "core/Config.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QDebug>
#include <QTimer>

namespace MCM {

MonitoringScreen::MonitoringScreen(DeviceDetector* detector, QWidget* parent)
    : QWidget(parent)
    , m_gridLayout(nullptr)
    , m_deviceDetector(detector)
{
    setupUi();
    createSlots();
    
    // Connect device detector
    connect(m_deviceDetector, &DeviceDetector::devicesChanged, 
            this, &MonitoringScreen::onDevicesChanged);
}

MonitoringScreen::~MonitoringScreen() {
    stopAllStreams();
    clearSlots();
}

void MonitoringScreen::setupUi() {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(10, 10, 10, 10);
    mainLayout->setSpacing(10);
    
    // Top bar with back button
    QHBoxLayout* topBar = new QHBoxLayout();
    
    m_backButton = new QPushButton("← Back", this);
    m_backButton->setObjectName("backButton");
    m_backButton->setFixedSize(100, 36);
    m_backButton->setCursor(Qt::PointingHandCursor);
    
    connect(m_backButton, &QPushButton::clicked, this, &MonitoringScreen::backRequested);
    
    topBar->addWidget(m_backButton);
    topBar->addStretch();
    
    QLabel* titleLabel = new QLabel("Camera Monitoring", this);
    titleLabel->setObjectName("screenTitle");
    QFont titleFont = titleLabel->font();
    titleFont.setPointSize(18);
    titleFont.setBold(true);
    titleLabel->setFont(titleFont);
    
    topBar->addWidget(titleLabel);
    topBar->addStretch();
    
    // Play button to start unplayed slots with sources
    m_playButton = new QPushButton("▶ Play All", this);
    m_playButton->setObjectName("playButton");
    m_playButton->setFixedSize(100, 36);
    m_playButton->setCursor(Qt::PointingHandCursor);
    m_playButton->setStyleSheet(
        "QPushButton { "
        "  background-color: #4CAF50; "
        "  color: white; "
        "  border: none; "
        "  border-radius: 4px; "
        "  font-weight: bold; "
        "} "
        "QPushButton:hover { "
        "  background-color: #45a049; "
        "} "
        "QPushButton:pressed { "
        "  background-color: #3d8b40; "
        "}"
    );
    connect(m_playButton, &QPushButton::clicked, this, &MonitoringScreen::onPlayButtonClicked);
    topBar->addWidget(m_playButton);
    
    mainLayout->addLayout(topBar);
    
    // Grid container
    QWidget* gridContainer = new QWidget(this);
    gridContainer->setObjectName("gridContainer");
    m_gridLayout = new QGridLayout(gridContainer);
    m_gridLayout->setContentsMargins(5, 5, 5, 5);
    m_gridLayout->setSpacing(8);
    
    mainLayout->addWidget(gridContainer, 1);
}

void MonitoringScreen::createSlots() {
    clearSlots();
    
    const auto& config = Config::instance();
    int maxSlots = config.grid().maxSlots();
    int rows = config.grid().rows;
    int columns = config.grid().columns;
    
    for (int i = 0; i < maxSlots; ++i) {
        CameraSlot* slot = new CameraSlot(i, m_deviceDetector, this);
        
        // Connect double-click signal
        connect(slot, &CameraSlot::doubleClicked, this, &MonitoringScreen::onSlotDoubleClicked);
        
        // Calculate grid position
        int row = i / columns;
        int col = i % columns;
        
        m_gridLayout->addWidget(slot, row, col);
        m_slots.append(slot);
    }
    
    qDebug() << "MonitoringScreen: Created" << maxSlots << "slots in" 
             << rows << "x" << columns << "grid";
}

void MonitoringScreen::clearSlots() {
    for (CameraSlot* slot : m_slots) {
        slot->stopStream();
        m_gridLayout->removeWidget(slot);
        delete slot;
    }
    m_slots.clear();
}

void MonitoringScreen::startAllStreams() {
    m_streaming = true;
    
    // Stagger camera starts to prevent overwhelming USB/system resources
    // Each camera gets 500ms to initialize before the next one starts
    // This prevents "Failed to start video surface due to main thread blocked"
    for (int i = 0; i < m_slots.size(); ++i) {
        CameraSlot* slot = m_slots[i];
        QTimer::singleShot(i * 500, slot, [slot]() {
            slot->startStream();
        });
    }
    
    qDebug() << "MonitoringScreen: Scheduled staggered start for" << m_slots.size() << "streams (500ms intervals)";
}

void MonitoringScreen::stopAllStreams() {
    m_streaming = false;
    for (CameraSlot* slot : m_slots) {
        slot->stopStream();
    }
    qDebug() << "MonitoringScreen: Stopped all streams";
}

void MonitoringScreen::rebuildGrid() {
    bool wasStreaming = m_streaming;
    
    if (wasStreaming) {
        stopAllStreams();
    }
    
    createSlots();
    
    if (wasStreaming) {
        startAllStreams();
    }
}

void MonitoringScreen::onSlotDoubleClicked(int slotIndex) {
    if (slotIndex < 0 || slotIndex >= m_slots.size()) {
        return;
    }
    
    CameraSlot* sourceSlot = m_slots[slotIndex];
    
    // Create expanded view window
    ExpandedView* expandedView = new ExpandedView(slotIndex, nullptr);
    expandedView->setAttribute(Qt::WA_DeleteOnClose);
    
    // Connect frame updates
    connect(sourceSlot, &CameraSlot::frameUpdated, expandedView, &ExpandedView::updateFrame);
    
    expandedView->show();
    
    qDebug() << "MonitoringScreen: Opened expanded view for slot" << slotIndex;
}

void MonitoringScreen::onDevicesChanged() {
    // Notify slots about device changes
    for (CameraSlot* slot : m_slots) {
        slot->refreshDeviceList();
    }
}

void MonitoringScreen::onPlayButtonClicked() {
    qDebug() << "MonitoringScreen: Play button clicked";
    
    int startedCount = 0;
    int skippedPlaying = 0;
    int skippedNoSource = 0;
    
    // Start only slots that have a source selected but are not playing
    for (int i = 0; i < m_slots.size(); ++i) {
        CameraSlot* slot = m_slots[i];
        
        if (slot->isStreaming()) {
            // Already playing - skip
            skippedPlaying++;
            continue;
        }
        
        if (!slot->hasSourceSelected()) {
            // No source selected - skip
            skippedNoSource++;
            continue;
        }
        
        // Has source but not playing - start with stagger
        QTimer::singleShot(startedCount * 500, slot, [slot]() {
            slot->startStream();
        });
        startedCount++;
    }
    
    qDebug() << "MonitoringScreen: Play All -" 
             << startedCount << "started," 
             << skippedPlaying << "already playing,"
             << skippedNoSource << "no source";
    
    if (startedCount > 0) {
        m_streaming = true;
    }
}

} // namespace MCM

