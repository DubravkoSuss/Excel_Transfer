#ifndef YEARCARD_H
#define YEARCARD_H

#include <QFrame>
#include <QVBoxLayout>
#include <QToolButton>
#include <QPushButton>
#include <QCheckBox>
#include <QVector>

class PeriodRow;

class YearCard : public QFrame
{
    Q_OBJECT

public:
    explicit YearCard(int year, QWidget* parent = nullptr);

    int  year()    const { return m_year; }
    bool isEmpty() const;
    bool isExpanded() const { return m_toggle->isChecked(); }

    void addRow(PeriodRow* row);
    void removeRow(PeriodRow* row);
    void clearRows();           // Disconnect + delete all PeriodRows safely
    void setExpanded(bool expanded);
    
    // Hybrid transfer helper methods
    void deselectAllMonths();
    void selectMonth(int monthIndex);

    const QVector<PeriodRow*>& rows() const { return m_rows; }

signals:
    void yearChecked(int year, bool checked);
    void quarterToggled(int year, int quarter, bool checked);
    void createMonthFilesRequested(int year, bool active);

public slots:
    void applyQuarter(int quarter, bool select);

private slots:
    void toggle(bool checked);

private:
    int              m_year;
    QCheckBox*       m_yearCheckBox;
    QToolButton*     m_toggle;
    QWidget*         m_content;
    QVBoxLayout*     m_contentLayout;
    QVector<PeriodRow*> m_rows;
    QVector<QCheckBox*> m_monthCheckboxes;  // Track month checkboxes for hybrid transfer
    QPushButton*     m_quarterBtns[4] = {};
    QPushButton*     m_btnAll = nullptr;
    QPushButton*     m_btnNone = nullptr;
    QPushButton*     m_btnCreateFiles = nullptr;
};

#endif // YEARCARD_H
