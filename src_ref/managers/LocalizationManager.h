// Author: SeungJae Lee
// LocalizationManager interface: discovers language resources and manages JsonTranslator lifecycle.

#pragma once

#include "i18n/JsonTranslator.h"

#include <QList>
#include <QObject>
#include <QStringList>

#include <memory>
#include <optional>

class LocalizationManager : public QObject
{
    Q_OBJECT

public:
    struct Language
    {
        QString code;
        QString name;
        QString nativeName;
        QString filePath;
        bool builtIn = false;

        bool operator==(const Language &other) const
        {
            return code == other.code
                && name == other.name
                && nativeName == other.nativeName
                && filePath == other.filePath
                && builtIn == other.builtIn;
        }
    };

    explicit LocalizationManager(QObject *parent = nullptr);

    void setSearchPaths(const QStringList &paths);
    QStringList searchPaths() const { return m_searchPaths; }

    void refreshLanguages();
    QList<Language> languages() const { return m_languages; }

    QString currentLanguageCode() const { return m_currentLanguage; }
    QString defaultLanguageCode() const { return m_defaultLanguage; }

    bool applyLanguage(const QString &code);

signals:
    void languagesChanged();
    void languageChanged(const QString &code);

private:
    QStringList m_searchPaths;    // directories searched for JSON language files
    QList<Language> m_languages;  // cached language metadata
    QString m_currentLanguage;
    QString m_defaultLanguage = QStringLiteral("en");
    std::unique_ptr<JsonTranslator> m_translator; // active translator installed in qApp

    static std::optional<Language> loadMetadata(const QString &filePath);
};
