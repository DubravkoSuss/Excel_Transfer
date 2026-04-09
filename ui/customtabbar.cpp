#include "customtabbar.h"
#include <QApplication>

CustomTabBar::CustomTabBar(QWidget* parent)
    : QTabBar(parent)
{
    setMovable(true);
}

void CustomTabBar::mouseMoveEvent(QMouseEvent* event)
{
    // Get the tab bar geometry
    QRect tabBarRect = rect();
    
    // Constrain the mouse position to stay within the tab bar bounds
    QPoint constrainedPos = event->pos();
    
    // Add padding to prevent tabs from going too far
    int leftPadding = 10;
    int rightPadding = 10;
    
    if (constrainedPos.x() < leftPadding) {
        constrainedPos.setX(leftPadding);
    } else if (constrainedPos.x() > tabBarRect.width() - rightPadding) {
        constrainedPos.setX(tabBarRect.width() - rightPadding);
    }
    
    // Create a new event with the constrained position
    QMouseEvent constrainedEvent(
        event->type(),
        constrainedPos,
        event->globalPosition(),
        event->button(),
        event->buttons(),
        event->modifiers()
    );
    
    // Call the base class implementation with constrained position
    QTabBar::mouseMoveEvent(&constrainedEvent);
}

void CustomTabBar::mouseReleaseEvent(QMouseEvent* event)
{
    m_isDragging = false;
    m_dragStartIndex = -1;
    QTabBar::mouseReleaseEvent(event);
}

CustomTabWidget::CustomTabWidget(QWidget* parent)
    : QTabWidget(parent)
{
    // Create and set custom tab bar
    CustomTabBar* customBar = new CustomTabBar(this);
    setTabBar(customBar);
}
