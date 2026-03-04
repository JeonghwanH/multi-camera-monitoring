// Author: SeungJae Lee
// StringUtils: small helpers for canonicalizing labels and generating stream-safe keys.

#include "StringUtils.h"

#include <QLatin1Char>

#include <algorithm>

namespace StringUtils
{
QString canonicalString(const QString &value)
{
    const QString trimmed = value.trimmed();
    QString normalized;
    normalized.reserve(trimmed.size());
    bool lastWasSpace = false;
    for (const QChar &ch : trimmed)
    {
        if (ch.isSpace())
        {
            if (lastWasSpace)
                continue;
            normalized.append(QLatin1Char(' '));
            lastWasSpace = true;
        }
        else
        {
            normalized.append(ch);
            lastWasSpace = false;
        }
    }
    return normalized.toCaseFolded();
}

QString sanitizeForStreamKey(const QString &source)
{
    QString result;
    result.reserve(source.size());
    bool lastWasUnderscore = false;
    for (const QChar &ch : source)
    {
        if (ch.isLetterOrNumber())
        {
            result.append(ch.toCaseFolded());
            lastWasUnderscore = false;
        }
        else
        {
            if (!result.isEmpty() && !lastWasUnderscore)
            {
                result.append(QLatin1Char('_'));
                lastWasUnderscore = true;
            }
        }
    }
    while (result.endsWith(QLatin1Char('_')))
        result.chop(1);
    return result;
}

QString streamKeyFromAlias(const QString &alias, const QString &fallback)
{
    QString key = sanitizeForStreamKey(alias);
    if (key.isEmpty())
        key = sanitizeForStreamKey(fallback);
    if (key.isEmpty())
        key = QStringLiteral("camera");
    return key;
}
} // namespace StringUtils
