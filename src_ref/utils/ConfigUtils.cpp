// Author: SeungJae Lee
// ConfigUtils: load/save helper that merges user overrides with bundled JSON defaults.

#include "ConfigUtils.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QJsonParseError>
#include <QStandardPaths>
#include <QJsonObject>

#include <utility>

namespace
{
constexpr auto kConfigResourcePath = ":/config/config.json";

QJsonDocument parseDocument(const QByteArray &data, const QString &source)
{
    QJsonParseError error{};
    const QJsonDocument doc = QJsonDocument::fromJson(data, &error);
    if (error.error != QJsonParseError::NoError)
    {
        qWarning() << "Failed to parse config document from" << source << ":" << error.errorString() << "offset"
                   << error.offset;
        return {};
    }
    return doc;
}
} // namespace

namespace ConfigUtils
{
QString writableConfigPath()
{
    const QString baseDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (baseDir.isEmpty())
        return QString();

    QDir dir(baseDir);
    if (!dir.exists() && !dir.mkpath(QStringLiteral(".")))
    {
        qWarning() << "Failed to create app data directory" << baseDir;
        return QString();
    }

    return dir.filePath(QStringLiteral("config.json"));
}

QJsonDocument loadConfig()
{
    const QString userPath = writableConfigPath();
    if (!userPath.isEmpty())
    {
        QFile userFile(userPath);
        if (userFile.exists() && userFile.open(QIODevice::ReadOnly))
        {
            const QJsonDocument doc = parseDocument(userFile.readAll(), userPath);
            if (!doc.isNull())
                return doc;
        }
    }

    QFile resource(kConfigResourcePath);
    if (!resource.open(QIODevice::ReadOnly))
    {
        qWarning() << "Failed to open config resource" << kConfigResourcePath << resource.errorString();
        return {};
    }

    const QByteArray data = resource.readAll();
    const QJsonDocument doc = parseDocument(data, kConfigResourcePath);
    if (doc.isNull())
        return doc;

    if (!userPath.isEmpty() && !QFile::exists(userPath))
    {
        QFile out(userPath);
        if (out.open(QIODevice::WriteOnly | QIODevice::Truncate))
        {
            // First run: seed user-writable config with bundled defaults so future edits persist.
            if (out.write(data) < 0)
                qWarning() << "Failed to initialize config file at" << userPath << out.errorString();
        }
        else
        {
            qWarning() << "Failed to create config file" << userPath << out.errorString();
        }
    }

    return doc;
}

bool saveConfig(const QJsonDocument &document)
{
    const QString path = writableConfigPath();
    if (path.isEmpty())
    {
        qWarning() << "No writable config path available";
        return false;
    }

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        qWarning() << "Failed to open config file for writing" << path << file.errorString();
        return false;
    }

    const QByteArray payload = document.toJson(QJsonDocument::Indented);
    if (file.write(payload) != payload.size())
    {
        qWarning() << "Failed to write full config file to" << path << file.errorString();
        return false;
    }

    return true;
}

bool showLegend()
{
    QJsonDocument doc = loadConfig();
    if (!doc.isObject())
        return true;

    QJsonObject root = doc.object();
    QJsonObject globalObj = root.value(QStringLiteral("global")).toObject();

    if (!globalObj.contains(QStringLiteral("showLegend")))
    {
        globalObj.insert(QStringLiteral("showLegend"), true);
        root.insert(QStringLiteral("global"), globalObj);
        doc.setObject(root);
        saveConfig(doc);
        return true;
    }

    return globalObj.value(QStringLiteral("showLegend")).toBool(true);
}
} // namespace ConfigUtils
