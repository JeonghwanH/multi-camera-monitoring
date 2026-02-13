#include "HomeScreen.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFont>
#include <QGraphicsDropShadowEffect>

namespace MCM {

HomeScreen::HomeScreen(QWidget* parent)
    : QWidget(parent)
{
    setupUi();
}

void HomeScreen::setupUi() {
    // Main layout
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(60, 60, 60, 60);
    mainLayout->setSpacing(40);
    
    // Title
    m_titleLabel = new QLabel("Multi-Camera Monitor", this);
    m_titleLabel->setObjectName("homeTitle");
    m_titleLabel->setAlignment(Qt::AlignCenter);
    
    QFont titleFont = m_titleLabel->font();
    titleFont.setPointSize(42);
    titleFont.setBold(true);
    m_titleLabel->setFont(titleFont);
    
    mainLayout->addWidget(m_titleLabel);
    mainLayout->addSpacing(40);
    
    // Buttons container
    QHBoxLayout* buttonLayout = new QHBoxLayout();
    buttonLayout->setSpacing(60);
    
    // Streaming button
    m_streamingButton = createNavButton("▶  STREAMING");
    m_streamingButton->setObjectName("streamingButton");
    
    // Settings button
    m_settingsButton = createNavButton("⚙  SETTINGS");
    m_settingsButton->setObjectName("settingsButton");
    
    buttonLayout->addStretch();
    buttonLayout->addWidget(m_streamingButton);
    buttonLayout->addWidget(m_settingsButton);
    buttonLayout->addStretch();
    
    mainLayout->addLayout(buttonLayout);
    mainLayout->addStretch();
    
    // Version label
    QLabel* versionLabel = new QLabel("Version 1.0.0", this);
    versionLabel->setObjectName("versionLabel");
    versionLabel->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(versionLabel);
    
    // Connect signals
    connect(m_streamingButton, &QPushButton::clicked, this, &HomeScreen::streamingClicked);
    connect(m_settingsButton, &QPushButton::clicked, this, &HomeScreen::settingsClicked);
}

QPushButton* HomeScreen::createNavButton(const QString& text, const QString& iconPath) {
    QPushButton* button = new QPushButton(text, this);
    button->setMinimumSize(320, 200);
    button->setMaximumSize(400, 250);
    button->setCursor(Qt::PointingHandCursor);
    
    QFont buttonFont = button->font();
    buttonFont.setPointSize(24);
    buttonFont.setBold(true);
    button->setFont(buttonFont);
    
    // Add shadow effect
    QGraphicsDropShadowEffect* shadow = new QGraphicsDropShadowEffect(button);
    shadow->setBlurRadius(20);
    shadow->setColor(QColor(0, 0, 0, 80));
    shadow->setOffset(0, 4);
    button->setGraphicsEffect(shadow);
    
    if (!iconPath.isEmpty()) {
        button->setIcon(QIcon(iconPath));
        button->setIconSize(QSize(48, 48));
    }
    
    return button;
}

} // namespace MCM

