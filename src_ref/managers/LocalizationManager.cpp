// Author: SeungJae Lee
// LocalizationManager: discovers JSON translation files, manages translators, and applies runtime language changes.

#include "LocalizationManager.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSet>
#include <QResource>

#include <algorithm>
#include <utility>

LocalizationManager::LocalizationManager(QObject *parent)
    : QObject(parent)
{
    Q_INIT_RESOURCE(app_languages);
    QStringList defaultPaths;
    const QString appDir = QCoreApplication::applicationDirPath();
    defaultPaths << QDir(appDir).filePath(QStringLiteral("languages"));
    defaultPaths << QDir(appDir).filePath(QStringLiteral("../languages"));
    defaultPaths << QDir(appDir).filePath(QStringLiteral("../../languages"));
    defaultPaths << QDir(appDir).filePath(QStringLiteral("../resources/languages"));
    defaultPaths << QDir(appDir).filePath(QStringLiteral("../../resources/languages"));
#if defined(Q_OS_MAC)
    defaultPaths << QDir(appDir).filePath(QStringLiteral("../Resources/languages"));
    defaultPaths << QDir(appDir).filePath(QStringLiteral("../../Resources/languages"));
#endif
    defaultPaths << QStringLiteral(":/resources/languages");
    defaultPaths << QStringLiteral(":/languages");
    setSearchPaths(defaultPaths);
    applyLanguage(m_defaultLanguage);
}

void LocalizationManager::setSearchPaths(const QStringList &paths)
{
    m_searchPaths.clear();
    for (const QString &path : paths)
    {
        if (path.isEmpty())
            continue;
        QDir dir(path);
        const QString absolute = dir.absolutePath();
        if (!m_searchPaths.contains(absolute))
            m_searchPaths << absolute;
    }

    refreshLanguages();
}

void LocalizationManager::refreshLanguages()
{
    // Rescan all configured paths to build the available language list (default English always first).
    qInfo() << "[Localization] scanning language paths:" << m_searchPaths;
    QList<Language> languages;

    Language english;
    english.code = m_defaultLanguage;
    english.name = QStringLiteral("English");
    english.nativeName = QStringLiteral("English");
    english.filePath.clear();
    english.builtIn = true;
    languages.append(english);

    QSet<QString> seenCodes;
    seenCodes.insert(m_defaultLanguage);

    for (const QString &searchPath : std::as_const(m_searchPaths))
    {
        QDir dir(searchPath);
        const bool exists = dir.exists();
        qInfo() << "[Localization] checking path" << searchPath << "exists=" << exists;
        if (!exists)
            continue;

        const QFileInfoList files = dir.entryInfoList(QStringList{QStringLiteral("*.json")}, QDir::Files | QDir::Readable);
        qInfo() << "[Localization] candidate files in" << searchPath << ":";
        for (const QFileInfo &info : files)
        {
            qInfo() << "  ->" << info.filePath();
            const auto metadata = loadMetadata(info.absoluteFilePath());
            if (!metadata.has_value())
            {
                qWarning() << "[Localization] failed to load metadata from" << info.filePath();
                continue;
            }

            if (seenCodes.contains(metadata->code))
                continue;

            seenCodes.insert(metadata->code);
            languages.append(metadata.value());
        }
    }

    if (languages.size() > 1)
    {
        std::sort(languages.begin() + 1, languages.end(), [](const Language &a, const Language &b) {
            return a.name.toCaseFolded() < b.name.toCaseFolded();
        });
    }

    const bool changed = languages != m_languages;
    QStringList codes;
    for (const auto &language : languages)
        codes << language.code;
    qInfo() << "[Localization] discovered languages:" << codes;
    m_languages = languages;

    if (changed)
        emit languagesChanged();
}

bool LocalizationManager::applyLanguage(const QString &code)
{
    // Swap translators at runtime; fall back to default language when no match is found.
    const QString targetCode = code.isEmpty() ? m_defaultLanguage : code;
    if (targetCode == m_currentLanguage)
        return true;

    std::unique_ptr<JsonTranslator> previousTranslator;
    const QString previousLanguage = m_currentLanguage;
    if (m_translator)
    {
        previousTranslator = std::move(m_translator);
        qApp->removeTranslator(previousTranslator.get());
    }

    if (targetCode == m_defaultLanguage)
    {
        m_currentLanguage = targetCode;
        emit languageChanged(m_currentLanguage);
        return true;
    }

    const auto it = std::find_if(m_languages.cbegin(), m_languages.cend(), [&targetCode](const Language &language) {
        return language.code == targetCode;
    });

    if (it == m_languages.cend() || it->filePath.isEmpty())
    {
        if (previousTranslator)
        {
            qApp->installTranslator(previousTranslator.get());
            m_translator = std::move(previousTranslator);
            m_currentLanguage = previousLanguage;
        }
        else
        {
            m_currentLanguage = m_defaultLanguage;
        }
        return false;
    }

    auto translator = std::make_unique<JsonTranslator>();
    if (!translator->loadFromFile(it->filePath))
    {
        if (previousTranslator)
        {
            qApp->installTranslator(previousTranslator.get());
            m_translator = std::move(previousTranslator);
            m_currentLanguage = previousLanguage;
        }
        else
        {
            m_currentLanguage = m_defaultLanguage;
        }
        return false;
    }

    if (!qApp->installTranslator(translator.get()))
    {
        if (previousTranslator)
        {
            qApp->installTranslator(previousTranslator.get());
            m_translator = std::move(previousTranslator);
            m_currentLanguage = previousLanguage;
        }
        else
        {
            m_currentLanguage = m_defaultLanguage;
        }
        return false;
    }

    m_translator = std::move(translator);
    m_currentLanguage = targetCode;
    emit languageChanged(m_currentLanguage);
    return true;
}

std::optional<LocalizationManager::Language> LocalizationManager::loadMetadata(const QString &filePath)
{
    // Parse meta section of JSON translation file to extract language identifiers.
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        qWarning() << "[Localization] cannot open language file" << filePath; 
        return std::nullopt;
    }

    const QByteArray data = file.readAll();
    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(data, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
    {
        qWarning() << "[Localization] invalid JSON in" << filePath << "error=" << error.errorString();
        return std::nullopt;
    }

    const QJsonObject root = document.object();
    const QJsonObject meta = root.value(QStringLiteral("meta")).toObject();

    const QString code = meta.value(QStringLiteral("code")).toString().trimmed();
    const QString name = meta.value(QStringLiteral("name")).toString().trimmed();

    if (code.isEmpty() || name.isEmpty())
    {
        qWarning() << "[Localization] missing code/name in" << filePath;
        return std::nullopt;
    }

    Language language;
    language.code = code;
    language.name = name;
    language.nativeName = meta.value(QStringLiteral("nativeName")).toString(name);
    language.filePath = filePath;
    language.builtIn = false;
    qInfo() << "[Localization] loaded language" << language.code << "(" << language.name << ") from" << filePath;
    return language;
}
