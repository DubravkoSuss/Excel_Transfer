// Header file
#ifndef TOASTWIDGET_H
#define TOASTWIDGET_H

#include <QWidget>
#include <QVBoxLayout>
#include <QLabel>
#include <QTimer>
#include <QPropertyAnimation>

class ToastWidget : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(qreal opacity READ opacity WRITE setOpacity)

public:
    enum ToastType {
        Info,
        Success,
        Warning,
        Error
    };

    explicit ToastWidget(QWidget *parent = nullptr);
    ~ToastWidget() override;

    void showToast(const QString& message, ToastType type = Info, int duration = 3000);

    qreal opacity() const { return m_opacity; }
    void setOpacity(qreal opacity);

signals:
    void toastClosed();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;

private slots:
    void onFadeInFinished();
    void onFadeOutFinished();
    void onTimeout();

private:
    void setupUI();
    void setToastStyle(ToastType type);

    QVBoxLayout* m_mainLayout;
    QLabel* m_messageLabel;

    QTimer* m_timer;
    QPropertyAnimation* m_fadeInAnimation;
    QPropertyAnimation* m_fadeOutAnimation;

    qreal m_opacity;
    ToastType m_currentType;
};

#endif // TOASTWIDGET_H