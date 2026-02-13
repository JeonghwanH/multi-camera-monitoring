#ifndef SETTINGSSCREEN_H
#define SETTINGSSCREEN_H

#include <QWidget>
#include <QSpinBox>
#include <QCheckBox>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>

namespace MCM {

/**
 * @brief Settings configuration screen
 * 
 * Allows users to modify:
 * - Grid settings (max slots, rows, columns)
 * - Buffer settings (frame count, min maintenance)
 * - Recording settings (enabled, chunk duration, output dir, fps, codec)
 */
class SettingsScreen : public QWidget {
    Q_OBJECT

public:
    explicit SettingsScreen(QWidget* parent = nullptr);

    /**
     * @brief Load current settings into UI
     */
    void loadCurrentSettings();

signals:
    void backRequested();
    void settingsChanged();

private slots:
    void saveSettings();
    void resetToDefaults();
    void browseOutputDirectory();

private:
    void setupUi();
    QWidget* createGridSection();
    QWidget* createBufferSection();
    QWidget* createRecordingSection();

    // Grid settings
    QSpinBox* m_maxSlotsSpinBox;
    QSpinBox* m_rowsSpinBox;
    QSpinBox* m_columnsSpinBox;

    // Buffer settings
    QSpinBox* m_frameCountSpinBox;
    QSpinBox* m_minMaintenanceSpinBox;
    QSpinBox* m_displayFpsSpinBox;

    // Recording settings
    QCheckBox* m_recordingEnabledCheckBox;
    QSpinBox* m_chunkDurationSpinBox;
    QLineEdit* m_outputDirectoryEdit;
    QSpinBox* m_fpsSpinBox;
    QComboBox* m_codecComboBox;

    // Buttons
    QPushButton* m_backButton;
    QPushButton* m_saveButton;
    QPushButton* m_resetButton;
};

} // namespace MCM

#endif // SETTINGSSCREEN_H

