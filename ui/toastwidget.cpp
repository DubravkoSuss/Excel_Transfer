// Source file
#include "toastwidget.h"
#include <QPainter>
#include <QMouseEvent>
#include <QApplication>
#include <QScreen>

ToastWidget::ToastWidget(QWidget *parent)
    : QWidget(parent)
    , m_opacity(0.0)
    , m_currentType(Info)
{
    setupUI();
}

ToastWidget::~ToastWidget()
{
}

void ToastWidget::setupUI()
{
    setWindowFlags(Qt::Tool | Qt::WindowStaysOnTopHint | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_DeleteOnClose);

    m_mainLayout = new QVBoxLayout(this);
    m_mainLayout->setContentsMargins(15, 15, 15, 15);
    m_mainLayout->setSpacing(0);

    m_messageLabel = new QLabel();
    m_messageLabel->setStyleSheet(
        "color: white;"
        "font-size: 14px;"
        "font-weight: 500;"
        "padding: 10px;"
        "background: transparent;"
        "border: none;"
    );
    m_messageLabel->setWordWrap(true);
    m_mainLayout->addWidget(m_messageLabel);

    m_timer = new QTimer(this);
    m_timer->setSingleShot(true);
    connect(m_timer, &QTimer::timeout, this, &ToastWidget::onTimeout);

    m_fadeInAnimation = new QPropertyAnimation(this, "opacity");
    m_fadeInAnimation->setDuration(300);
    m_fadeInAnimation->setStartValue(0.0);
    m_fadeInAnimation->setEndValue(0.9);
    connect(m_fadeInAnimation, &QPropertyAnimation::finished, this, &ToastWidget::onFadeInFinished);

    m_fadeOutAnimation = new QPropertyAnimation(this, "opacity");
    m_fadeOutAnimation->setDuration(300);
    m_fadeOutAnimation->setStartValue(0.9);
    m_fadeOutAnimation->setEndValue(0.0);
    connect(m_fadeOutAnimation, &QPropertyAnimation::finished, this, &ToastWidget::onFadeOutFinished);

    setFixedWidth(350);
    setFixedHeight(100);
    hide();
}

void ToastWidget::showToast(const QString& message, ToastType type, int duration)
{
    m_messageLabel->setText(message);
    setToastStyle(type);

    // Position at bottom-center of the parent window.
    // If no parent, fall back to center-bottom of primary screen.
    if (parentWidget()) {
        QPoint parentPos = parentWidget()->mapToGlobal(QPoint(0, 0));
        QSize  parentSize = parentWidget()->size();
        int x = parentPos.x() + (parentSize.width()  - width())  / 2;
        int y = parentPos.y() +  parentSize.height()  - height() - 30;
        move(x, y);
    } else {
        QRect sg = QApplication::primaryScreen()->availableGeometry();
        move(sg.left() + (sg.width() - width()) / 2,
             sg.top()  +  sg.height() - height() - 50);
    }

    // Show and fade in
    show();
    m_fadeInAnimation->start();
    m_timer->start(duration);
}

void ToastWidget::setToastStyle(ToastType type)
{
    QString bgColor;
    switch (type) {
        case Success:
            bgColor = "#059669"; // Green
            break;
        case Warning:
            bgColor = "#F59E0B"; // Orange
            break;
        case Error:
            bgColor = "#DC2626"; // Red
            break;
        case Info:
        default:
            bgColor = "#3B82F6"; // Blue
            break;
    }

    setStyleSheet(
        QString("background-color: %1; border-radius: 8px;").arg(bgColor)
    );
}

void ToastWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setOpacity(m_opacity);

    // Draw background
    QRect rect = contentsRect();
    QString bgColor;
    switch (m_currentType) {
        case Success:
            bgColor = "#059669";
            break;
        case Warning:
            bgColor = "#F59E0B";
            break;
        case Error:
            bgColor = "#DC2626";
            break;
        case Info:
        default:
            bgColor = "#3B82F6";
            break;
    }

    painter.setBrush(QColor(bgColor));
    painter.setPen(Qt::NoPen);
    painter.drawRoundedRect(rect, 8, 8);
}

void ToastWidget::mousePressEvent(QMouseEvent *event)
{
    Q_UNUSED(event);
    m_timer->stop();
    m_fadeOutAnimation->start();
}

void ToastWidget::onFadeInFinished()
{
    // Fade in complete
}

void ToastWidget::onFadeOutFinished()
{
    hide();
    emit toastClosed();
}

void ToastWidget::onTimeout()
{
    m_fadeOutAnimation->start();
}

void ToastWidget::setOpacity(qreal opacity)
{
    m_opacity = opacity;
    update();
}