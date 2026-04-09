#include "periodrow.h"
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include "../../core/mappingsmanager.h"

static const QStringList kMonths = {"January", "February", "March", "April", "May", "June",
                                    "July", "August", "September", "October", "November", "December"};

PeriodRow::PeriodRow(int year, QWidget* parent)
    : QFrame(parent), m_year(year), m_layout(new QVBoxLayout(this))
{
    buildUI();
}

PeriodRow::~PeriodRow() = default;

void PeriodRow::buildUI()
{
    setObjectName("periodRow");
    setStyleSheet("#periodRow { background: #F8F9FC; border: none; border-radius: 8px; }");

    m_layout->setContentsMargins(10, 10, 10, 10);

    QHBoxLayout* mainRowLayout = new QHBoxLayout();
    
    // Year label on the left
    QLabel* yearLabel = new QLabel(QString::number(m_year));
    yearLabel->setFixedWidth(60);
    yearLabel->setStyleSheet("font-weight: 600; font-size: 13px;");
    mainRowLayout->addWidget(yearLabel);

    // Grid layout for months (4 rows x 3 columns)
    QGridLayout* monthsGrid = new QGridLayout();
    monthsGrid->setSpacing(8);
    monthsGrid->setContentsMargins(10, 0, 10, 0);

    int row = 0;
    int col = 0;
    for (const QString& month : kMonths) {
        QCheckBox* cb = new QCheckBox(month.left(3));
        cb->setStyleSheet("font-size: 12px;");
        connect(cb, &QCheckBox::checkStateChanged, this, [this, month](Qt::CheckState state) {
            emit monthSelected(m_year, month, state == Qt::Checked);
            emit monthsChanged();
        });
        m_monthCheckboxes[month] = cb;
        monthsGrid->addWidget(cb, row, col);
        
        col++;
        if (col >= 3) {  // 3 months per row
            col = 0;
            row++;
        }
    }

    mainRowLayout->addLayout(monthsGrid);
    mainRowLayout->addStretch();

    m_layout->addLayout(mainRowLayout);
}

QStringList PeriodRow::getSelectedMonths() const
{
    QStringList selected;
    for (auto it = m_monthCheckboxes.constBegin(); it != m_monthCheckboxes.constEnd(); ++it) {
        if (it.value()->isChecked()) {
            selected.append(it.key());
        }
    }
    return selected;
}

bool PeriodRow::isEmpty() const
{
    for (auto it = m_monthCheckboxes.constBegin(); it != m_monthCheckboxes.constEnd(); ++it) {
        if (it.value()->isChecked()) return false;
    }
    return true;
}

void PeriodRow::setMonthChecked(const QString& month, bool checked)
{
    auto it = m_monthCheckboxes.find(month);
    if (it != m_monthCheckboxes.end()) {
        it.value()->setChecked(checked);
    }
}

void PeriodRow::buildQuickSelector(QVBoxLayout* layout)
{
    m_quickSelector = new QuarterQuickSelector(this);
    m_quickSelector->setAvailableYears({m_year});

    connect(m_quickSelector, &QuarterQuickSelector::quarterSelected, this,
            [this](int year, int quarter, bool selected) {
        if (year != m_year) {
            return;
        }
        const QList<int> months = MappingsManager::QUARTERS.value(quarter);
        for (int monthIndex : months) {
            if (monthIndex < 0 || monthIndex >= kMonths.size()) {
                continue;
            }
            const QString& monthName = kMonths[monthIndex];
            setMonthChecked(monthName, selected);
        }
    });

    layout->addWidget(m_quickSelector);
}

void PeriodRow::onMonthCheckChanged(const QString& month, int state)
{
    Q_UNUSED(month);
    Q_UNUSED(state);
}

QVector<QCheckBox*> PeriodRow::monthCheckboxesInOrder() const
{
    QVector<QCheckBox*> result;
    result.reserve(kMonths.size());
    for (const QString& month : kMonths) {
        auto it = m_monthCheckboxes.find(month);
        if (it != m_monthCheckboxes.end()) {
            result.append(it.value());
        }
    }
    return result;
}
