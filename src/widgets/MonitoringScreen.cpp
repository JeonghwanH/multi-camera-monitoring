#include "MonitoringScreen.h"
#include "CameraSlot.h"
#include "ExpandedView.h"
#include "utils/DeviceDetector.h"
#include "core/Config.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QDebug>

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
    
    // Placeholder for symmetry
    QWidget* placeholder = new QWidget(this);
    placeholder->setFixedWidth(100);
    topBar->addWidget(placeholder);
    
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
    int maxSlots = config.grid().maxSlots;
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
    for (CameraSlot* slot : m_slots) {
        slot->startStream();
    }
    qDebug() << "MonitoringScreen: Started all streams";
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

} // namespace MCM

