// Author: SeungJae Lee
// AnalysisStatusPanel interface: lightweight widget exposing weld pass and alignment indicators.

#pragma once

#include <QWidget>
#include <QString>
#include <QVector>

#include <optional>
#include <functional>

class QLabel;
class QVBoxLayout;
class WeldAlignmentWidget;

class AnalysisStatusPanel : public QWidget
{
    Q_OBJECT

public:
    explicit AnalysisStatusPanel(QWidget *parent = nullptr);

    QWidget *addCustomSection(const std::function<void(QWidget *, QVBoxLayout *)> &builder);

    void setPassInfo(const QString &passLabel);
    void setAlignmentOffset(const std::optional<double> &offsetMm,
                            const std::optional<double> &warningThresholdMm = std::nullopt);
    void clear();

private:
    QLabel *createLabel(QWidget *parent = nullptr) const;
    QWidget *createSection(const std::function<void(QWidget *, QVBoxLayout *)> &builder);
    void updateAlignmentDisplay();

    QVBoxLayout *m_panelLayout = nullptr;
    bool m_firstSection = true;

    QLabel *m_passLabel = nullptr;
    QLabel *m_passIndicatorLabel = nullptr;
    WeldAlignmentWidget *m_alignmentWidget = nullptr;
    QLabel *m_alignmentValueLabel = nullptr;

    QString m_passValue;
    std::optional<double> m_alignmentOffset;
    std::optional<double> m_alignmentWarningThreshold;
};
