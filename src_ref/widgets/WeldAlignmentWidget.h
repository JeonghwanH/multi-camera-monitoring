// Author: SeungJae Lee
// WeldAlignmentWidget interface: renders welding nozzle alignment versus seam centerline.

#pragma once

#include <QWidget>

class WeldAlignmentWidget : public QWidget
{
    Q_OBJECT

public:
    explicit WeldAlignmentWidget(QWidget *parent = nullptr);

    double offset() const;
    void setOffset(double offset);
    void setWarningActive(bool warning);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    double m_offset = 0.0; // Normalized range [-1.0, 1.0]
    bool m_warningActive = false;
};
