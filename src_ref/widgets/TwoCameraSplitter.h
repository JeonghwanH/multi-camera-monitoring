// Author: SeungJae Lee
// TwoCameraSplitter interface: horizontal splitter with stylised handle for dual camera previews.

#pragma once

#include <QSplitter>
#include <QSplitterHandle>

class TwoCameraSplitter : public QSplitter
{
    Q_OBJECT

public:
    explicit TwoCameraSplitter(QWidget *parent = nullptr);

protected:
    QSplitterHandle *createHandle() override;
};

class TwoCameraSplitterHandle : public QSplitterHandle
{
public:
    TwoCameraSplitterHandle(Qt::Orientation orientation, QSplitter *splitter);

protected:
    void paintEvent(QPaintEvent *event) override;
    QSize sizeHint() const override;
};
