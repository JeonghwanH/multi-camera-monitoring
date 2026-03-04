// Author: SeungJae Lee
// ToggleSwitch interface: custom Qt button styled as on/off pill.

#pragma once

#include <QAbstractButton>

class ToggleSwitch : public QAbstractButton
{
    Q_OBJECT

public:
    explicit ToggleSwitch(QWidget *parent = nullptr);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;
};
