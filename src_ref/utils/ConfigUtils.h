// Author: SeungJae Lee
// ConfigUtils interface: read/write JSON configuration from user or resource storage.

#pragma once

#include <QJsonDocument>
#include <QString>

namespace ConfigUtils
{
QJsonDocument loadConfig();
bool saveConfig(const QJsonDocument &document);
QString writableConfigPath();
bool showLegend();
} // namespace ConfigUtils
