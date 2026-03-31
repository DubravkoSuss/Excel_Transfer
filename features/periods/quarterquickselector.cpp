// Include necessary Qt headers at the beginning of quarterquickselector.cpp
#include "quarterquickselector.h"
#include <QDebug>

const QMap<int, QString> QuarterQuickSelector::QUARTER_NAMES = {
    {1, "Q1"},
    {2, "Q2"},
    {3, "Q3"},
    {4, "Q4"}
};

QuarterQuickSelector::QuarterQuickSelector(QWidget *parent)
    : QWidget(parent)
    , m_availableYears({2023, 2024, 2025, 2026, 2027, 2028})
{
    setupUI();
}

QuarterQuickSelector::~QuarterQuickSelector()
{
}

void QuarterQuickSelector::setupUI()
{
    m_mainLayout = new QVBoxLayout(this);
    m_mainLayout->setContentsMargins(0, 0, 0, 0);

    m_quartersGroup = new QGroupBox("Quick Quarter Selector");
    m_quartersGroup->setStyleSheet(
        "QGroupBox {"
        "    font-weight: 600;"
        "    font-size: 14px;"
        "    margin-top: 10px;"
        "}"
        "QGroupBox::title {"
        "    subcontrol-origin: margin;"
        "    left: 10px;"
        "    padding: 0 5px 0 5px;"
        "}"
    );

    m_quartersGrid = new QGridLayout(m_quartersGroup);
    m_quartersGrid->setSpacing(8);
    m_quartersGrid->setContentsMargins(10, 20, 10, 10);

    createQuarterCheckboxes();

    m_mainLayout->addWidget(m_quartersGroup);
}

void QuarterQuickSelector::createQuarterCheckboxes()
{
    int row = 0;
    int col = 0;

    for (int year : m_availableYears) {
        for (int quarter = 1; quarter <= 4; ++quarter) {
            QString label = QString("%1 %2").arg(year).arg(quarterToString(quarter));
            QCheckBox* checkBox = new QCheckBox(label);
            checkBox->setStyleSheet(
                "QCheckBox {"
                "    padding: 6px 8px;"
                "    font-size: 12px;"
                "}"
                "QCheckBox:hover {"
                "    background-color: #F3F4F6;"
                "    border-radius: 4px;"
                "}"
            );

            QPair<int, int> key(year, quarter);
            m_quarterCheckboxes[key] = checkBox;

            connect(checkBox, &QCheckBox::checkStateChanged, this, &QuarterQuickSelector::onQuarterCheckBoxChanged);

            m_quartersGrid->addWidget(checkBox, row, col);
            col++;

            if (col > 3) {
                col = 0;
                row++;
            }
        }
    }
}

QString QuarterQuickSelector::quarterToString(int quarter) const
{
    return QUARTER_NAMES.value(quarter, QString("Q%1").arg(quarter));
}

void QuarterQuickSelector::onQuarterCheckBoxChanged(Qt::CheckState state)
{
    QCheckBox* senderCheckBox = qobject_cast<QCheckBox*>(sender());
    if (!senderCheckBox) return;

    // Find which quarter was changed
    for (auto it = m_quarterCheckboxes.begin(); it != m_quarterCheckboxes.end(); ++it) {
        if (it.value() == senderCheckBox) {
            int year = it.key().first;
            int quarter = it.key().second;
            bool selected = (state == Qt::Checked);

            emit quarterSelected(year, quarter, selected);
            emit selectionChanged();
            break;
        }
    }
}

QVector<QuarterQuickSelector::QuarterSelection> QuarterQuickSelector::getSelectedQuarters() const
{
    QVector<QuarterSelection> selected;

    for (auto it = m_quarterCheckboxes.begin(); it != m_quarterCheckboxes.end(); ++it) {
        if (it.value()->isChecked()) {
            QuarterSelection selection;
            selection.year = it.key().first;
            selection.quarter = it.key().second;
            selection.selected = true;
            selected.append(selection);
        }
    }

    return selected;
}

void QuarterQuickSelector::selectQuarter(int year, int quarter, bool selected)
{
    QPair<int, int> key(year, quarter);
    if (m_quarterCheckboxes.contains(key)) {
        m_quarterCheckboxes[key]->setChecked(selected);
    }
}

void QuarterQuickSelector::clearAllSelections()
{
    for (QCheckBox* checkBox : m_quarterCheckboxes.values()) {
        checkBox->setChecked(false);
    }
    emit selectionChanged();
}

void QuarterQuickSelector::setAvailableYears(const QVector<int>& years)
{
    m_availableYears = years;

    // Clear existing checkboxes
    while (QLayoutItem* item = m_quartersGrid->takeAt(0)) {
        if (item->widget()) {
            item->widget()->deleteLater();
        }
        delete item;
    }
    m_quarterCheckboxes.clear();

    // Create new checkboxes
    createQuarterCheckboxes();
}
