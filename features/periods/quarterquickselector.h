#ifndef FEATURES_PERIODS_QUARTERQUICKSELECTOR_H
#define FEATURES_PERIODS_QUARTERQUICKSELECTOR_H

// Include necessary Qt headers at the beginning of quarterquickselector.h
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QCheckBox>
#include <QGridLayout>
#include <QMap>
#include <QPair>

class QuarterQuickSelector : public QWidget
{
    Q_OBJECT

public:
    explicit QuarterQuickSelector(QWidget *parent = nullptr);
    ~QuarterQuickSelector() override;

    struct QuarterSelection {
        int year;
        int quarter;
        bool selected;
    };
    QVector<QuarterSelection> getSelectedQuarters() const;
    void selectQuarter(int year, int quarter, bool selected);
    void clearAllSelections();
    void setAvailableYears(const QVector<int>& years);

signals:
    void quarterSelected(int year, int quarter, bool selected);
    void selectionChanged();

private slots:
    void onQuarterCheckBoxChanged(Qt::CheckState state);

private:
    void setupUI();
    void createQuarterCheckboxes();
    QString quarterToString(int quarter) const;

    QVBoxLayout* m_mainLayout;
    QGroupBox* m_quartersGroup;
    QGridLayout* m_quartersGrid;
    QMap<QPair<int, int>, QCheckBox*> m_quarterCheckboxes; // key: (year, quarter)
    QVector<int> m_availableYears;

    static const QMap<int, QString> QUARTER_NAMES;
};

#endif // FEATURES_PERIODS_QUARTERQUICKSELECTOR_H

