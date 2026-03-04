// Author: SeungJae Lee
// AiClientPayloadBuilder interface: constructs payload JSON used for AI model startup.

#pragma once

#include <QJsonObject>
#include <QStringList>
#include <QVariant>

namespace AiClientPayloadBuilder
{
QJsonObject buildModelStartupPayload(const QString &streamKey,
                                     const QJsonObject &config,
                                     const QStringList &weldAnalysisTypeArgs,
                                     const QString &refTypeArg,
                                     const QVariant &refScaleArg);
}
