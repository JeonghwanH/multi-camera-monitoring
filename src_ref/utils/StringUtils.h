// Author: SeungJae Lee
// StringUtils interface: normalization helpers for display labels and analysis stream keys.

#pragma once

#include <QString>

namespace StringUtils
{
QString canonicalString(const QString &value);
QString sanitizeForStreamKey(const QString &source);
QString streamKeyFromAlias(const QString &alias, const QString &fallback);
} // namespace StringUtils
