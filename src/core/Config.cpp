#include "Config.h"
#include <QDebug>

namespace MCM {

// GridConfig implementation
QJsonObject GridConfig::toJson() const {
    return QJsonObject{
        {"maxSlots", maxSlots},
        {"rows", rows},
        {"columns", columns}
    };
}

GridConfig GridConfig::fromJson(const QJsonObject& obj) {
    GridConfig config;
    config.maxSlots = obj.value("maxSlots").toInt(8);
    config.rows = obj.value("rows").toInt(2);
    config.columns = obj.value("columns").toInt(4);
    return config;
}

// BufferConfig implementation
QJsonObject BufferConfig::toJson() const {
    return QJsonObject{
        {"frameCount", frameCount},
        {"minMaintenance", minMaintenance},
        {"displayFps", displayFps}
    };
}

BufferConfig BufferConfig::fromJson(const QJsonObject& obj) {
    BufferConfig config;
    config.frameCount = obj.value("frameCount").toInt(30);
    config.minMaintenance = obj.value("minMaintenance").toInt(10);
    config.displayFps = obj.value("displayFps").toInt(30);
    return config;
}

// RecordingConfig implementation
QJsonObject RecordingConfig::toJson() const {
    return QJsonObject{
        {"enabled", enabled},
        {"chunkDurationSeconds", chunkDurationSeconds},
        {"outputDirectory", outputDirectory},
        {"fps", fps},
        {"codec", codec}
    };
}

RecordingConfig RecordingConfig::fromJson(const QJsonObject& obj) {
    RecordingConfig config;
    config.enabled = obj.value("enabled").toBool(true);
    config.chunkDurationSeconds = obj.value("chunkDurationSeconds").toInt(300);
    config.outputDirectory = obj.value("outputDirectory").toString("recordings");
    config.fps = obj.value("fps").toInt(30);
    config.codec = obj.value("codec").toString("mp4v");
    return config;
}

// SlotConfig implementation
QString SlotConfig::sourceTypeToString(SourceType type) {
    switch (type) {
        case SourceType::None: return "none";
        case SourceType::Auto: return "auto";
        case SourceType::Wired: return "wired";
        case SourceType::Rtsp: return "rtsp";
        default: return "auto";
    }
}

SourceType SlotConfig::stringToSourceType(const QString& str) {
    if (str == "none") return SourceType::None;
    if (str == "auto") return SourceType::Auto;
    if (str == "wired") return SourceType::Wired;
    if (str == "rtsp") return SourceType::Rtsp;
    return SourceType::Auto;
}

QJsonObject SlotConfig::toJson() const {
    return QJsonObject{
        {"type", sourceTypeToString(type)},
        {"source", source}
    };
}

SlotConfig SlotConfig::fromJson(const QJsonObject& obj) {
    SlotConfig config;
    config.type = stringToSourceType(obj.value("type").toString("auto"));
    config.source = obj.value("source").toString();
    return config;
}

// Config implementation
Config& Config::instance() {
    static Config instance;
    return instance;
}

Config::Config() {
    initializeDefaults();
}

void Config::initializeDefaults() {
    m_grid = GridConfig();
    m_buffer = BufferConfig();
    m_recording = RecordingConfig();
    
    m_slots.clear();
    for (int i = 0; i < m_grid.maxSlots; ++i) {
        SlotConfig slot;
        slot.type = SourceType::Auto;
        slot.source = QString::number(i);  // Slot index = device index
        m_slots.push_back(slot);
    }
}

bool Config::load(const QString& path) {
    QMutexLocker locker(&m_mutex);
    
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "Could not open config file:" << path;
        initializeDefaults();
        return false;
    }
    
    QByteArray data = file.readAll();
    file.close();
    
    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(data, &error);
    
    if (error.error != QJsonParseError::NoError) {
        qWarning() << "JSON parse error:" << error.errorString();
        initializeDefaults();
        return false;
    }
    
    QJsonObject root = doc.object();
    
    // Parse grid config
    if (root.contains("grid")) {
        m_grid = GridConfig::fromJson(root.value("grid").toObject());
    }
    
    // Parse buffer config
    if (root.contains("buffer")) {
        m_buffer = BufferConfig::fromJson(root.value("buffer").toObject());
    }
    
    // Parse recording config
    if (root.contains("recording")) {
        m_recording = RecordingConfig::fromJson(root.value("recording").toObject());
    }
    
    // Parse slots config
    m_slots.clear();
    if (root.contains("slots")) {
        QJsonArray slotsArray = root.value("slots").toArray();
        for (const QJsonValue& val : slotsArray) {
            m_slots.push_back(SlotConfig::fromJson(val.toObject()));
        }
    }
    
    // Ensure we have enough slots
    while (static_cast<int>(m_slots.size()) < m_grid.maxSlots) {
        SlotConfig slot;
        slot.type = SourceType::Auto;
        slot.source = QString::number(m_slots.size());
        m_slots.push_back(slot);
    }
    
    m_configPath = path;
    qDebug() << "Config loaded from:" << path;
    return true;
}

bool Config::save(const QString& path) {
    QMutexLocker locker(&m_mutex);
    
    QJsonObject root;
    root["grid"] = m_grid.toJson();
    root["buffer"] = m_buffer.toJson();
    root["recording"] = m_recording.toJson();
    
    QJsonArray slotsArray;
    for (const auto& slot : m_slots) {
        slotsArray.append(slot.toJson());
    }
    root["slots"] = slotsArray;
    
    QJsonDocument doc(root);
    
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning() << "Could not open config file for writing:" << path;
        return false;
    }
    
    file.write(doc.toJson(QJsonDocument::Indented));
    file.close();
    
    m_configPath = path;
    qDebug() << "Config saved to:" << path;
    return true;
}

const SlotConfig& Config::slot(int index) const {
    QMutexLocker locker(&m_mutex);
    static SlotConfig defaultSlot;
    
    if (index >= 0 && index < static_cast<int>(m_slots.size())) {
        return m_slots[index];
    }
    return defaultSlot;
}

void Config::setGrid(const GridConfig& config) {
    QMutexLocker locker(&m_mutex);
    m_grid = config;
    
    // Adjust slots array if needed
    while (static_cast<int>(m_slots.size()) < m_grid.maxSlots) {
        SlotConfig slot;
        slot.type = SourceType::Auto;
        slot.source = QString::number(m_slots.size());
        m_slots.push_back(slot);
    }
}

void Config::setBuffer(const BufferConfig& config) {
    QMutexLocker locker(&m_mutex);
    m_buffer = config;
}

void Config::setRecording(const RecordingConfig& config) {
    QMutexLocker locker(&m_mutex);
    m_recording = config;
}

void Config::setSlot(int index, const SlotConfig& config) {
    QMutexLocker locker(&m_mutex);
    if (index >= 0 && index < static_cast<int>(m_slots.size())) {
        m_slots[index] = config;
    }
}

void Config::resetToDefaults() {
    QMutexLocker locker(&m_mutex);
    initializeDefaults();
}

} // namespace MCM

