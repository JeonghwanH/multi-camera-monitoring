// Author: SeungJae Lee
// TwoCameraSplitter: custom splitter with centred handle sized for twin camera layout.

#include "TwoCameraSplitter.h"

#include <QPainter>
#include <QPaintEvent>

#include <algorithm>

namespace
{
constexpr int kHandleWidth = 4;
constexpr int kHandleHeight = 80;
}

TwoCameraSplitter::TwoCameraSplitter(QWidget *parent)
    : QSplitter(Qt::Horizontal, parent)
{
    setChildrenCollapsible(false);
    setHandleWidth(kHandleWidth);
    setOpaqueResize(true);
}

QSplitterHandle *TwoCameraSplitter::createHandle()
{
    return new TwoCameraSplitterHandle(orientation(), this);
}

TwoCameraSplitterHandle::TwoCameraSplitterHandle(Qt::Orientation orientation, QSplitter *splitter)
    : QSplitterHandle(orientation, splitter)
{
    setCursor(Qt::SplitHCursor);
}

QSize TwoCameraSplitterHandle::sizeHint() const
{
    QSize hint = QSplitterHandle::sizeHint();
    if (orientation() == Qt::Horizontal)
    {
        hint.setWidth(kHandleWidth);
        hint.setHeight(std::max(hint.height(), kHandleHeight));
    }
    return hint;
}

void TwoCameraSplitterHandle::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(QStringLiteral("#696E77")));

    QRect handleRect = rect();
    if (orientation() == Qt::Horizontal)
    {
        handleRect.setHeight(kHandleHeight);
        handleRect.moveTop((height() - kHandleHeight) / 2);
    }

    painter.drawRoundedRect(handleRect, 2, 2);
}
