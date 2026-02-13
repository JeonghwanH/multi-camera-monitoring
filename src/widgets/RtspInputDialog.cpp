#include "RtspInputDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QRegularExpression>

namespace MCM {

RtspInputDialog::RtspInputDialog(QWidget* parent)
    : QDialog(parent)
{
    setupUi();
}

void RtspInputDialog::setupUi() {
    setWindowTitle("Enter RTSP URL");
    setMinimumWidth(500);
    setModal(true);
    
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(15);
    mainLayout->setContentsMargins(20, 20, 20, 20);
    
    // Title
    QLabel* titleLabel = new QLabel("RTSP Stream URL", this);
    QFont titleFont = titleLabel->font();
    titleFont.setPointSize(14);
    titleFont.setBold(true);
    titleLabel->setFont(titleFont);
    mainLayout->addWidget(titleLabel);
    
    // Help text
    QLabel* helpLabel = new QLabel(
        "Enter the RTSP stream URL. Format: rtsp://[username:password@]host[:port]/path",
        this
    );
    helpLabel->setWordWrap(true);
    helpLabel->setStyleSheet("color: #888;");
    mainLayout->addWidget(helpLabel);
    
    // URL input
    m_urlEdit = new QLineEdit(this);
    m_urlEdit->setPlaceholderText("rtsp://192.168.1.100:554/stream1");
    m_urlEdit->setMinimumHeight(36);
    connect(m_urlEdit, &QLineEdit::textChanged, this, &RtspInputDialog::onUrlChanged);
    connect(m_urlEdit, &QLineEdit::returnPressed, this, &RtspInputDialog::onOkClicked);
    mainLayout->addWidget(m_urlEdit);
    
    // Error label
    m_errorLabel = new QLabel(this);
    m_errorLabel->setStyleSheet("color: #e74c3c;");
    m_errorLabel->hide();
    mainLayout->addWidget(m_errorLabel);
    
    // Examples
    QLabel* examplesLabel = new QLabel(
        "Examples:\n"
        "• rtsp://192.168.1.100:554/stream1\n"
        "• rtsp://admin:password@192.168.1.100:554/live\n"
        "• rtsp://camera.local/Streaming/Channels/1",
        this
    );
    examplesLabel->setStyleSheet("color: #666; font-size: 11px;");
    mainLayout->addWidget(examplesLabel);
    
    mainLayout->addStretch();
    
    // Buttons
    QHBoxLayout* buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();
    
    m_cancelButton = new QPushButton("Cancel", this);
    m_cancelButton->setCursor(Qt::PointingHandCursor);
    connect(m_cancelButton, &QPushButton::clicked, this, &QDialog::reject);
    
    m_okButton = new QPushButton("Connect", this);
    m_okButton->setObjectName("primaryButton");
    m_okButton->setCursor(Qt::PointingHandCursor);
    m_okButton->setEnabled(false);
    connect(m_okButton, &QPushButton::clicked, this, &RtspInputDialog::onOkClicked);
    
    buttonLayout->addWidget(m_cancelButton);
    buttonLayout->addWidget(m_okButton);
    
    mainLayout->addLayout(buttonLayout);
    
    // Focus on input
    m_urlEdit->setFocus();
}

QString RtspInputDialog::url() const {
    return m_urlEdit->text().trimmed();
}

void RtspInputDialog::setUrl(const QString& url) {
    m_urlEdit->setText(url);
}

void RtspInputDialog::onUrlChanged(const QString& text) {
    QString trimmed = text.trimmed();
    bool valid = validateUrl(trimmed);
    m_okButton->setEnabled(valid);
    
    if (!trimmed.isEmpty() && !valid) {
        m_errorLabel->setText("Invalid RTSP URL format");
        m_errorLabel->show();
    } else {
        m_errorLabel->hide();
    }
}

void RtspInputDialog::onOkClicked() {
    if (validateUrl(url())) {
        accept();
    }
}

bool RtspInputDialog::validateUrl(const QString& url) const {
    if (url.isEmpty()) {
        return false;
    }
    
    // Basic RTSP URL validation
    QRegularExpression regex(
        R"(^rtsp://([a-zA-Z0-9._-]+:[^@]+@)?[a-zA-Z0-9._-]+(:\d+)?(/.*)?$)",
        QRegularExpression::CaseInsensitiveOption
    );
    
    return regex.match(url).hasMatch();
}

} // namespace MCM

