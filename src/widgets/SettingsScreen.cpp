#include "SettingsScreen.h"
#include "core/Config.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QFileDialog>
#include <QMessageBox>
#include <QScrollArea>

namespace MCM {

SettingsScreen::SettingsScreen(QWidget* parent)
    : QWidget(parent)
{
    setupUi();
    loadCurrentSettings();
}

void SettingsScreen::setupUi() {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(20, 20, 20, 20);
    mainLayout->setSpacing(15);

    // Top bar
    QHBoxLayout* topBar = new QHBoxLayout();
    
    m_backButton = new QPushButton("← Back", this);
    m_backButton->setObjectName("backButton");
    m_backButton->setFixedSize(100, 36);
    m_backButton->setCursor(Qt::PointingHandCursor);
    connect(m_backButton, &QPushButton::clicked, this, &SettingsScreen::backRequested);
    
    QLabel* titleLabel = new QLabel("Settings", this);
    titleLabel->setObjectName("screenTitle");
    QFont titleFont = titleLabel->font();
    titleFont.setPointSize(18);
    titleFont.setBold(true);
    titleLabel->setFont(titleFont);
    
    topBar->addWidget(m_backButton);
    topBar->addStretch();
    topBar->addWidget(titleLabel);
    topBar->addStretch();
    
    QWidget* placeholder = new QWidget(this);
    placeholder->setFixedWidth(100);
    topBar->addWidget(placeholder);
    
    mainLayout->addLayout(topBar);

    // Scroll area for settings
    QScrollArea* scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    
    QWidget* scrollContent = new QWidget();
    QVBoxLayout* contentLayout = new QVBoxLayout(scrollContent);
    contentLayout->setSpacing(20);

    // Add sections
    contentLayout->addWidget(createGridSection());
    contentLayout->addWidget(createBufferSection());
    contentLayout->addWidget(createRecordingSection());
    contentLayout->addStretch();

    scrollArea->setWidget(scrollContent);
    mainLayout->addWidget(scrollArea, 1);

    // Bottom buttons
    QHBoxLayout* buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();
    
    m_resetButton = new QPushButton("Reset to Defaults", this);
    m_resetButton->setObjectName("resetButton");
    m_resetButton->setCursor(Qt::PointingHandCursor);
    connect(m_resetButton, &QPushButton::clicked, this, &SettingsScreen::resetToDefaults);
    
    m_saveButton = new QPushButton("Save Settings", this);
    m_saveButton->setObjectName("saveButton");
    m_saveButton->setCursor(Qt::PointingHandCursor);
    connect(m_saveButton, &QPushButton::clicked, this, &SettingsScreen::saveSettings);
    
    buttonLayout->addWidget(m_resetButton);
    buttonLayout->addSpacing(20);
    buttonLayout->addWidget(m_saveButton);
    
    mainLayout->addLayout(buttonLayout);
}

QWidget* SettingsScreen::createGridSection() {
    QGroupBox* group = new QGroupBox("Grid Configuration", this);
    group->setObjectName("settingsGroup");
    
    QGridLayout* layout = new QGridLayout(group);
    layout->setSpacing(15);
    
    // Rows
    layout->addWidget(new QLabel("Grid Rows:", this), 0, 0);
    m_rowsSpinBox = new QSpinBox(this);
    m_rowsSpinBox->setRange(1, 8);
    m_rowsSpinBox->setToolTip("Number of rows in the camera grid");
    layout->addWidget(m_rowsSpinBox, 0, 1);
    
    // Columns
    layout->addWidget(new QLabel("Grid Columns:", this), 1, 0);
    m_columnsSpinBox = new QSpinBox(this);
    m_columnsSpinBox->setRange(1, 8);
    m_columnsSpinBox->setToolTip("Number of columns in the camera grid");
    layout->addWidget(m_columnsSpinBox, 1, 1);
    
    // Total slots (computed, read-only)
    layout->addWidget(new QLabel("Total Slots:", this), 2, 0);
    m_totalSlotsLabel = new QLabel("8", this);
    m_totalSlotsLabel->setObjectName("computedLabel");
    m_totalSlotsLabel->setToolTip("Total slots = Rows × Columns (automatically computed)");
    layout->addWidget(m_totalSlotsLabel, 2, 1);
    
    // Connect rows/columns changes to update total
    connect(m_rowsSpinBox, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &SettingsScreen::updateTotalSlotsLabel);
    connect(m_columnsSpinBox, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &SettingsScreen::updateTotalSlotsLabel);
    
    layout->setColumnStretch(2, 1);
    
    return group;
}

void SettingsScreen::updateTotalSlotsLabel() {
    int total = m_rowsSpinBox->value() * m_columnsSpinBox->value();
    m_totalSlotsLabel->setText(QString::number(total));
}

QWidget* SettingsScreen::createBufferSection() {
    QGroupBox* group = new QGroupBox("Buffer Configuration", this);
    group->setObjectName("settingsGroup");
    
    QGridLayout* layout = new QGridLayout(group);
    layout->setSpacing(15);
    
    // Frame count
    layout->addWidget(new QLabel("Buffer Frame Count:", this), 0, 0);
    m_frameCountSpinBox = new QSpinBox(this);
    m_frameCountSpinBox->setRange(10, 120);
    m_frameCountSpinBox->setToolTip("Maximum frames to buffer per camera (10-120)");
    layout->addWidget(m_frameCountSpinBox, 0, 1);
    
    // Min maintenance
    layout->addWidget(new QLabel("Minimum Maintenance:", this), 1, 0);
    m_minMaintenanceSpinBox = new QSpinBox(this);
    m_minMaintenanceSpinBox->setRange(5, 60);
    m_minMaintenanceSpinBox->setToolTip("Minimum frames before playback starts (5-60)");
    layout->addWidget(m_minMaintenanceSpinBox, 1, 1);
    
    // Display FPS
    layout->addWidget(new QLabel("Display FPS:", this), 2, 0);
    m_displayFpsSpinBox = new QSpinBox(this);
    m_displayFpsSpinBox->setRange(5, 60);
    m_displayFpsSpinBox->setToolTip("Display refresh rate (5-60 fps)");
    layout->addWidget(m_displayFpsSpinBox, 2, 1);
    
    // Note
    QLabel* noteLabel = new QLabel("Higher values = smoother playback but more latency", this);
    noteLabel->setObjectName("noteLabel");
    layout->addWidget(noteLabel, 3, 0, 1, 2);
    
    layout->setColumnStretch(2, 1);
    
    return group;
}

QWidget* SettingsScreen::createRecordingSection() {
    QGroupBox* group = new QGroupBox("Recording Configuration", this);
    group->setObjectName("settingsGroup");
    
    QGridLayout* layout = new QGridLayout(group);
    layout->setSpacing(15);
    
    // Enabled
    m_recordingEnabledCheckBox = new QCheckBox("Enable Recording", this);
    layout->addWidget(m_recordingEnabledCheckBox, 0, 0, 1, 2);
    
    // Chunk duration
    layout->addWidget(new QLabel("Chunk Duration (seconds):", this), 1, 0);
    m_chunkDurationSpinBox = new QSpinBox(this);
    m_chunkDurationSpinBox->setRange(60, 3600);
    m_chunkDurationSpinBox->setSingleStep(60);
    m_chunkDurationSpinBox->setToolTip("Duration of each video chunk (60-3600 seconds)");
    layout->addWidget(m_chunkDurationSpinBox, 1, 1);
    
    // Output directory
    layout->addWidget(new QLabel("Output Directory:", this), 2, 0);
    QHBoxLayout* dirLayout = new QHBoxLayout();
    m_outputDirectoryEdit = new QLineEdit(this);
    m_outputDirectoryEdit->setPlaceholderText("Select output directory...");
    QPushButton* browseButton = new QPushButton("Browse...", this);
    browseButton->setCursor(Qt::PointingHandCursor);
    connect(browseButton, &QPushButton::clicked, this, &SettingsScreen::browseOutputDirectory);
    dirLayout->addWidget(m_outputDirectoryEdit);
    dirLayout->addWidget(browseButton);
    layout->addLayout(dirLayout, 2, 1);
    
    // FPS
    layout->addWidget(new QLabel("Recording FPS:", this), 3, 0);
    m_fpsSpinBox = new QSpinBox(this);
    m_fpsSpinBox->setRange(5, 60);
    m_fpsSpinBox->setToolTip("Frames per second for recording (15-60)");
    layout->addWidget(m_fpsSpinBox, 3, 1);
    
    // Codec
    layout->addWidget(new QLabel("Video Codec:", this), 4, 0);
    m_codecComboBox = new QComboBox(this);
    m_codecComboBox->addItems({"mp4v", "avc1", "xvid"});
    m_codecComboBox->setToolTip("Video codec for recording");
    layout->addWidget(m_codecComboBox, 4, 1);
    
    layout->setColumnStretch(2, 1);
    
    return group;
}

void SettingsScreen::loadCurrentSettings() {
    const auto& config = Config::instance();
    
    // Grid
    m_rowsSpinBox->setValue(config.grid().rows);
    m_columnsSpinBox->setValue(config.grid().columns);
    updateTotalSlotsLabel();  // Update computed total
    
    // Buffer
    m_frameCountSpinBox->setValue(config.buffer().frameCount);
    m_minMaintenanceSpinBox->setValue(config.buffer().minMaintenance);
    m_displayFpsSpinBox->setValue(config.buffer().displayFps);
    
    // Recording
    m_recordingEnabledCheckBox->setChecked(config.recording().enabled);
    m_chunkDurationSpinBox->setValue(config.recording().chunkDurationSeconds);
    m_outputDirectoryEdit->setText(config.recording().outputDirectory);
    m_fpsSpinBox->setValue(config.recording().fps);
    
    int codecIndex = m_codecComboBox->findText(config.recording().codec);
    if (codecIndex >= 0) {
        m_codecComboBox->setCurrentIndex(codecIndex);
    }
}

void SettingsScreen::saveSettings() {
    auto& config = Config::instance();
    
    // Grid - maxSlots is computed automatically as rows × columns
    GridConfig gridConfig;
    gridConfig.rows = m_rowsSpinBox->value();
    gridConfig.columns = m_columnsSpinBox->value();
    config.setGrid(gridConfig);
    
    // Buffer
    BufferConfig bufferConfig;
    bufferConfig.frameCount = m_frameCountSpinBox->value();
    bufferConfig.minMaintenance = m_minMaintenanceSpinBox->value();
    bufferConfig.displayFps = m_displayFpsSpinBox->value();
    config.setBuffer(bufferConfig);
    
    // Recording
    RecordingConfig recordingConfig;
    recordingConfig.enabled = m_recordingEnabledCheckBox->isChecked();
    recordingConfig.chunkDurationSeconds = m_chunkDurationSpinBox->value();
    recordingConfig.outputDirectory = m_outputDirectoryEdit->text();
    recordingConfig.fps = m_fpsSpinBox->value();
    recordingConfig.codec = m_codecComboBox->currentText();
    config.setRecording(recordingConfig);
    
    // Save to file
    config.save();
    
    QMessageBox::information(this, "Settings Saved", 
        "Settings have been saved successfully.\n"
        "Some changes will take effect when you restart streaming.");
    
    emit settingsChanged();
}

void SettingsScreen::resetToDefaults() {
    QMessageBox::StandardButton reply = QMessageBox::question(this, 
        "Reset to Defaults",
        "Are you sure you want to reset all settings to default values?",
        QMessageBox::Yes | QMessageBox::No);
    
    if (reply == QMessageBox::Yes) {
        Config::instance().resetToDefaults();
        loadCurrentSettings();
    }
}

void SettingsScreen::browseOutputDirectory() {
    QString dir = QFileDialog::getExistingDirectory(this, 
        "Select Output Directory",
        m_outputDirectoryEdit->text());
    
    if (!dir.isEmpty()) {
        m_outputDirectoryEdit->setText(dir);
    }
}

} // namespace MCM

