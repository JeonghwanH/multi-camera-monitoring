#ifndef CONFIG_H
#define CONFIG_H

#include <QString>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QFile>
#include <QDir>
#include <QMutex>
#include <QMutexLocker>
#include <vector>
#include <memory>

namespace MCM {

/**
 * @brief Grid configuration for camera slots layout
 */
struct GridConfig {
    int maxSlots = 8;
    int rows = 2;
    int columns = 4;
    
    QJsonObject toJson() const;
    static GridConfig fromJson(const QJsonObject& obj);
};

/**
 * @brief Buffer configuration for frame buffering
 */
struct BufferConfig {
    int frameCount = 30;       // Maximum frames in buffer
    int minMaintenance = 10;   // Minimum frames before playback starts
    int displayFps = 30;       // Display refresh rate
    
    QJsonObject toJson() const;
    static BufferConfig fromJson(const QJsonObject& obj);
};

/**
 * @brief Recording configuration for video saving
 */
struct RecordingConfig {
    bool enabled = true;
    int chunkDurationSeconds = 300;  // 5 minutes default
    QString outputDirectory = "recordings";
    int fps = 30;
    QString codec = "mp4v";
    
    QJsonObject toJson() const;
    static RecordingConfig fromJson(const QJsonObject& obj);
};

/**
 * @brief Source type enumeration
 */
enum class SourceType {
    None,    // No streaming
    Auto,    // Automatic (slot index = device index)
    Wired,   // Specific wired camera
    Rtsp     // RTSP stream
};

/**
 * @brief Per-slot configuration
 */
struct SlotConfig {
    SourceType type = SourceType::Auto;
    QString source;  // Device index for wired/auto, URL for RTSP
    
    QJsonObject toJson() const;
    static SlotConfig fromJson(const QJsonObject& obj);
    
    static QString sourceTypeToString(SourceType type);
    static SourceType stringToSourceType(const QString& str);
};

/**
 * @brief Main configuration manager (Singleton)
 */
class Config {
public:
    static Config& instance();
    
    // Prevent copying
    Config(const Config&) = delete;
    Config& operator=(const Config&) = delete;
    
    // Load/Save
    bool load(const QString& path = "config.json");
    bool save(const QString& path = "config.json");
    
    // Getters
    const GridConfig& grid() const { return m_grid; }
    const BufferConfig& buffer() const { return m_buffer; }
    const RecordingConfig& recording() const { return m_recording; }
    const SlotConfig& slot(int index) const;
    int slotCount() const { return static_cast<int>(m_slots.size()); }
    
    // Setters
    void setGrid(const GridConfig& config);
    void setBuffer(const BufferConfig& config);
    void setRecording(const RecordingConfig& config);
    void setSlot(int index, const SlotConfig& config);
    
    // Utility
    void resetToDefaults();
    QString configPath() const { return m_configPath; }

private:
    Config();
    ~Config() = default;
    
    void initializeDefaults();
    
    GridConfig m_grid;
    BufferConfig m_buffer;
    RecordingConfig m_recording;
    std::vector<SlotConfig> m_slots;
    QString m_configPath;
    
    mutable QMutex m_mutex;
};

} // namespace MCM

#endif // CONFIG_H

