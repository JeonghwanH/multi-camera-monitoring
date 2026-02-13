#ifndef HOMESCREEN_H
#define HOMESCREEN_H

#include <QWidget>
#include <QPushButton>
#include <QLabel>

namespace MCM {

/**
 * @brief Initial home screen with navigation buttons
 * 
 * Provides big rectangle buttons for:
 * - Streaming (camera monitoring)
 * - Settings (configuration)
 */
class HomeScreen : public QWidget {
    Q_OBJECT

public:
    explicit HomeScreen(QWidget* parent = nullptr);

signals:
    void streamingClicked();
    void settingsClicked();

private:
    void setupUi();
    QPushButton* createNavButton(const QString& text, const QString& iconPath = QString());

    QPushButton* m_streamingButton;
    QPushButton* m_settingsButton;
    QLabel* m_titleLabel;
};

} // namespace MCM

#endif // HOMESCREEN_H

