// Author: SeungJae Lee
// Application entry point: wires up global styling, tooltips, logging, and shows MainWindow.

#include "MainWindow.h"
#include "managers/AiClient.h"
#include "utils/ConfigUtils.h"
#include "utils/DebugConfig.h"
#include "utils/Logger.h"

#include <QApplication>
#include <QColor>
#include <QCoreApplication>
#include <QCursor>
#include <QGuiApplication>
#include <QHelpEvent>
#include <QIcon>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QObject>
#include <QPalette>
#include <QPointer>
#include <QScreen>
#include <QToolTip>
#include <QWindow>

namespace
{
class DarkToolTipController : public QObject
{
    Q_OBJECT

public:
    explicit DarkToolTipController(QObject *parent = nullptr)
        : QObject(parent)
    {
        m_tooltipLabel = new QLabel(nullptr, Qt::ToolTip | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::NoDropShadowWindowHint);
        m_tooltipLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
        m_tooltipLabel->setAttribute(Qt::WA_ShowWithoutActivating);
        m_tooltipLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        m_tooltipLabel->setWordWrap(false);
        m_tooltipLabel->setContentsMargins(12, 6, 12, 6);
        m_tooltipLabel->setStyleSheet(QStringLiteral(
            "QLabel{background-color:rgba(17,17,19,0.94);"
            "color:rgba(226,255,242,0.92);"
            "border:1px solid rgba(52,56,62,0.6);"
            "border-radius:8px;"
            "font-size:13px;font-weight:600;}"));
        m_tooltipLabel->hide();
    }

    bool eventFilter(QObject *watched, QEvent *event) override
    {
        switch (event->type())
        {
        case QEvent::ToolTip:
        {
            auto *helpEvent = static_cast<QHelpEvent *>(event);
            auto *widget = qobject_cast<QWidget *>(watched);
            if (!widget)
                break;
            const QString text = widget->toolTip();
            if (text.isEmpty())
            {
                hideTip();
                return true;
            }
            showTip(widget, helpEvent->globalPos(), text);
            return true;
        }
        case QEvent::ToolTipChange:
        {
            if (watched == m_currentWidget)
            {
                auto *widget = static_cast<QWidget *>(watched);
                const QString text = widget->toolTip();
                if (text.isEmpty())
                    hideTip();
                else
                    showTip(widget, QCursor::pos(), text);
            }
            break;
        }
        case QEvent::Leave:
        case QEvent::Hide:
        case QEvent::FocusOut:
        case QEvent::WindowDeactivate:
        case QEvent::MouseButtonPress:
        case QEvent::MouseButtonDblClick:
        case QEvent::Destroy:
            if (watched == m_currentWidget)
                hideTip();
            break;
        default:
            break;
        }
        return QObject::eventFilter(watched, event);
    }

private:
    void showTip(QWidget *widget, const QPoint &globalPos, const QString &text)
    {
        m_currentWidget = widget;
        m_tooltipLabel->setText(text);
        m_tooltipLabel->adjustSize();

        QPoint pos = globalPos + QPoint(16, 20);
        const QSize size = m_tooltipLabel->size();

        QScreen *screen = widget->window()->windowHandle()
                              ? widget->window()->windowHandle()->screen()
                              : QGuiApplication::screenAt(globalPos);
        if (!screen)
            screen = QGuiApplication::primaryScreen();

        if (screen)
        {
            const QRect available = screen->availableGeometry();
            if (pos.x() + size.width() > available.right())
                pos.setX(available.right() - size.width());
            if (pos.y() + size.height() > available.bottom())
                pos.setY(available.bottom() - size.height());
            if (pos.x() < available.left())
                pos.setX(available.left());
            if (pos.y() < available.top())
                pos.setY(available.top());
        }

        m_tooltipLabel->move(pos);
        m_tooltipLabel->show();
    }

    void hideTip()
    {
        m_currentWidget.clear();
        if (m_tooltipLabel->isVisible())
            m_tooltipLabel->hide();
    }

    QLabel *m_tooltipLabel = nullptr;
    QPointer<QWidget> m_currentWidget;
};
} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication::setAttribute(Qt::AA_UseStyleSheetPropagationInWidgetStyles);
    QApplication app(argc, argv);
    auto *tooltipController = new DarkToolTipController(&app);
    app.installEventFilter(tooltipController);
    Logger::initialize(QStringLiteral("Weldbeing"));

    const QJsonDocument configDoc = ConfigUtils::loadConfig();
    bool debugModeEnabled = false;
    if (configDoc.isObject())
    {
        const QJsonObject root = configDoc.object();
        const QJsonObject globalObj = root.value(QStringLiteral("global")).toObject();
        debugModeEnabled = globalObj.value(QStringLiteral("debugMode")).toBool(false);
    }
    DebugConfig::setDebugLoggingEnabled(debugModeEnabled);

    const QString tooltipAndDialogStyle = QStringLiteral(
        "QToolTip {"
        " color: rgba(226, 255, 242, 0.92);"
        " background-color: rgba(17, 17, 19, 0.94);"
        " border: 1px solid rgba(52, 56, 62, 0.6);"
        " border-radius: 8px;"
        " padding: 6px 12px;"
        " font-size: 13px;"
        " }"
        "QMessageBox {"
        " background-color: rgba(17, 17, 19, 0.94);"
        " color: rgba(226, 255, 242, 0.92);"
        " border: 1px solid rgba(52, 56, 62, 0.6);"
        " border-radius: 12px;"
        " }"
        "QMessageBox QLabel {"
        " color: rgba(226, 255, 242, 0.92);"
        " font-size: 13px;"
        " font-weight: 500;"
        " }"
        "QMessageBox QPushButton {"
        " background-color: rgba(0, 0, 0, 0.32);"
        " color: rgba(226, 255, 242, 0.92);"
        " border-radius: 8px;"
        " padding: 6px 16px;"
        " min-width: 88px;"
        " font-weight: 600;"
        " border: none;"
        " }"
        "QMessageBox QPushButton:hover {"
        " background-color: rgba(34, 255, 162, 0.24);"
        " }"
        "QMessageBox QPushButton:pressed {"
        " background-color: rgba(34, 255, 162, 0.32);"
        " }");
    QString combinedStyle = app.styleSheet();
    if (!combinedStyle.isEmpty())
        combinedStyle.append(QLatin1Char('\n'));
    combinedStyle.append(tooltipAndDialogStyle);
    app.setStyleSheet(combinedStyle);

    QPalette palette = app.palette();
    const QColor tooltipBg(17, 17, 19, 240);
    const QColor tooltipFg(226, 255, 242, 240);
    for (QPalette::ColorGroup group : {QPalette::Active, QPalette::Inactive, QPalette::Disabled})
    {
        palette.setColor(group, QPalette::ToolTipBase, tooltipBg);
        palette.setColor(group, QPalette::ToolTipText, tooltipFg);
    }
    app.setPalette(palette);
    QToolTip::setPalette(app.palette());

    app.setWindowIcon(QIcon(QStringLiteral(":/icons/logo.png")));
    AiClient::configureNoProxy(true);
    MainWindow mainWindow;
    mainWindow.setWindowIcon(QIcon(QStringLiteral(":/icons/logo.png")));
    mainWindow.show();
    return app.exec();
}

#include "main.moc"
