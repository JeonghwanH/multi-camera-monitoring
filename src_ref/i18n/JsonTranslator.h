// Author: SeungJae Lee
// JsonTranslator interface: JSON-based QTranslator supporting simple context + plural lookups.

#pragma once

#include <QHash>
#include <QTranslator>

class JsonTranslator : public QTranslator
{
public:
    explicit JsonTranslator(QObject *parent = nullptr);

    bool loadFromFile(const QString &filePath);

    QString languageCode() const { return m_languageCode; }
    QString languageName() const { return m_languageName; }

protected:
    QString translate(const char *context, const char *sourceText, const char *disambiguation, int n) const override;
    bool isEmpty() const override;

private:
    using ContextTranslations = QHash<QString, QString>;
    QHash<QString, ContextTranslations> m_translations; // context -> (source key -> translated string)
    QString m_languageCode;
    QString m_languageName;

    QString lookup(const QString &context, const QString &sourceText, const char *disambiguation, int n) const;
};
