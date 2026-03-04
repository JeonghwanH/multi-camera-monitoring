// Author: SeungJae Lee
// JsonTranslator: lightweight JSON-backed QTranslator for runtime language switching.

#include "JsonTranslator.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>

JsonTranslator::JsonTranslator(QObject *parent)
    : QTranslator(parent)
{
}

bool JsonTranslator::loadFromFile(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;

    const QByteArray data = file.readAll();
    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(data, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
        return false;

    const QJsonObject root = document.object();
    const QJsonObject meta = root.value(QStringLiteral("meta")).toObject();
    m_languageCode = meta.value(QStringLiteral("code")).toString().trimmed();
    m_languageName = meta.value(QStringLiteral("name")).toString().trimmed();

    m_translations.clear(); // reset cache before repopulating

    const QJsonObject strings = root.value(QStringLiteral("strings")).toObject();
    for (auto contextIt = strings.constBegin(); contextIt != strings.constEnd(); ++contextIt)
    {
        if (!contextIt.value().isObject())
            continue;

        const QString contextName = contextIt.key();
        const QJsonObject contextStrings = contextIt.value().toObject();
        ContextTranslations translations;

        for (auto stringIt = contextStrings.constBegin(); stringIt != contextStrings.constEnd(); ++stringIt)
        {
            const QString sourceKey = stringIt.key();
            const QJsonValue value = stringIt.value();

            if (value.isString())
            {
                translations.insert(sourceKey, value.toString());
                continue;
            }

            if (!value.isObject())
                continue;

            // Store plural forms using synthetic keys so translate() can pick them via count parameter.
            const QJsonObject pluralForms = value.toObject();
            const QString singular = pluralForms.value(QStringLiteral("one")).toString();
            const QString plural = pluralForms.value(QStringLiteral("other")).toString();

            if (!singular.isEmpty())
                translations.insert(QStringLiteral("%1|plural_one").arg(sourceKey), singular);
            if (!plural.isEmpty())
                translations.insert(QStringLiteral("%1|plural_other").arg(sourceKey), plural);
        }

        if (!translations.isEmpty())
            m_translations.insert(contextName, translations);
    }

    return true;
}

QString JsonTranslator::translate(const char *context, const char *sourceText, const char *disambiguation, int n) const
{
    const QString contextKey = context ? QString::fromUtf8(context) : QString();
    const QString sourceKey = sourceText ? QString::fromUtf8(sourceText) : QString();
    return lookup(contextKey, sourceKey, disambiguation, n);
}

bool JsonTranslator::isEmpty() const
{
    return m_translations.isEmpty();
}

QString JsonTranslator::lookup(const QString &context, const QString &sourceText, const char *disambiguation, int n) const
{
    const auto contextTranslations = m_translations.value(context);
    if (contextTranslations.isEmpty())
        return QString();

    QString lookupKey = sourceText;
    if (disambiguation && *disambiguation)
    {
        lookupKey += QLatin1Char('|');
        lookupKey += QString::fromUtf8(disambiguation);
    }

    const auto directIt = contextTranslations.constFind(lookupKey);
    if (directIt != contextTranslations.constEnd())
        return directIt.value();

    if (n >= 0)
    {
        // We encode plural variants as "source|plural_one/other"; pick matching form based on n.
        const QString pluralKey = (n == 1)
            ? QStringLiteral("%1|plural_one").arg(lookupKey)
            : QStringLiteral("%1|plural_other").arg(lookupKey);

        const auto pluralIt = contextTranslations.constFind(pluralKey);
        if (pluralIt != contextTranslations.constEnd())
            return pluralIt.value();
    }

    return QString();
}
