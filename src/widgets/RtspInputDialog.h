#ifndef RTSPINPUTDIALOG_H
#define RTSPINPUTDIALOG_H

#include <QDialog>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>

namespace MCM {

/**
 * @brief Dialog for entering RTSP URL
 * 
 * Provides a text input for RTSP stream URL with validation.
 */
class RtspInputDialog : public QDialog {
    Q_OBJECT

public:
    explicit RtspInputDialog(QWidget* parent = nullptr);

    /**
     * @brief Get the entered URL
     */
    QString url() const;

    /**
     * @brief Set the URL (for editing existing)
     */
    void setUrl(const QString& url);

private slots:
    void onUrlChanged(const QString& text);
    void onOkClicked();

private:
    void setupUi();
    bool validateUrl(const QString& url) const;

    QLineEdit* m_urlEdit;
    QPushButton* m_okButton;
    QPushButton* m_cancelButton;
    QLabel* m_errorLabel;
};

} // namespace MCM

#endif // RTSPINPUTDIALOG_H

