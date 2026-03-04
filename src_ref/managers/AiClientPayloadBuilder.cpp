// Author: SeungJae Lee
// AiClientPayloadBuilder: helper utilities for composing AI startup payloads from config overrides.

#include "AiClientPayloadBuilder.h"

#include <QJsonArray>

namespace
{
// Normalize weld analysis types and configuration fallbacks.
QStringList normalizedTypes(const QStringList &types)
{
    QStringList out;
    out.reserve(types.size());
    for (const QString &type : types)
    {
        const QString trimmed = type.trimmed();
        if (trimmed.isEmpty())
            continue;
        out << trimmed.toUpper();
    }
    return out;
}

QString extractRefTypeFromConfig(const QJsonObject &config)
{
    const QJsonObject server = config.value(QStringLiteral("analysisServer")).toObject();
    const QString serverRef = server.value(QStringLiteral("refType")).toString().trimmed();
    if (!serverRef.isEmpty())
        return serverRef;
    return config.value(QStringLiteral("refType")).toString().trimmed();
}

double extractRefScaleFromConfig(const QJsonObject &config, bool &found)
{
    const QJsonObject server = config.value(QStringLiteral("analysisServer")).toObject();
    const QJsonValue serverScale = server.value(QStringLiteral("refScale"));
    if (serverScale.isDouble())
    {
        found = true;
        return serverScale.toDouble();
    }
    if (serverScale.isString())
    {
        bool ok = false;
        const double converted = serverScale.toString().toDouble(&ok);
        if (ok)
        {
            found = true;
            return converted;
        }
    }

    const QJsonValue refScale = config.value(QStringLiteral("refScale"));
    if (refScale.isDouble())
    {
        found = true;
        return refScale.toDouble();
    }
    if (refScale.isString())
    {
        bool ok = false;
        const double converted = refScale.toString().toDouble(&ok);
        if (ok)
        {
            found = true;
            return converted;
        }
    }
    found = false;
    return 26.0;
}
} // namespace

namespace AiClientPayloadBuilder
{
QJsonObject buildModelStartupPayload(const QString &streamKey,
                                     const QJsonObject &config,
                                     const QStringList &weldAnalysisTypeArgs,
                                     const QString &refTypeArg,
                                     const QVariant &refScaleArg)
{
    QJsonObject body;
    body.insert(QStringLiteral("streamKey"), streamKey);

    QStringList types = weldAnalysisTypeArgs;
    if (types.isEmpty())
    {
        const QJsonValue configured = config.value(QStringLiteral("weldAnalysisType"));
        if (configured.isArray())
        {
            for (const QJsonValue &value : configured.toArray())
                types << value.toString();
        }
        else if (configured.isString())
        {
            types << configured.toString();
        }
    }
    if (types.isEmpty())
        types << QStringLiteral("FIRST");

    const QStringList normalized = normalizedTypes(types);
    QJsonArray typesArray;
    for (const QString &type : normalized)
        typesArray.append(type);
    body.insert(QStringLiteral("weldAnalysisType"), typesArray);

    QString refType = refTypeArg.trimmed();
    if (refType.isEmpty())
        refType = extractRefTypeFromConfig(config);
    if (refType.isEmpty())
        refType = QStringLiteral("ARC_TORCH");
    body.insert(QStringLiteral("refType"), refType);

    double scale = 26.0;
    if (!refScaleArg.isNull())
        scale = refScaleArg.toDouble();
    else
    {
        bool found = false;
        scale = extractRefScaleFromConfig(config, found);
        if (!found)
            scale = 26.0;
    }

    body.insert(QStringLiteral("refScale"), scale);
    return body;
}
} // namespace AiClientPayloadBuilder
