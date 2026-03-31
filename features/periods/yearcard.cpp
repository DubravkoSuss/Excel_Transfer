#include "yearcard.h"
#include "periodrow.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QCheckBox>

YearCard::YearCard(int year, QWidget* parent)
    : QFrame(parent)
    , m_year(year)
{
    setObjectName("yearCard");
    setStyleSheet(
        "#yearCard {"
        "    background: #FFFFFF;"
        "    border: 1px solid #D1D5DB;"
        "    border-radius: 10px;"
        "}"
    );

    QVBoxLayout* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->setSpacing(0);

    // ── Header row ───────────────────────────────────────────────────────────
    QWidget* header = new QWidget();
    header->setObjectName("yearHeader");
    header->setStyleSheet(
        "#yearHeader {"
        "  background: #EEF2FF;"
        "  border-top-left-radius: 10px;"
        "  border-top-right-radius: 10px;"
        "  border-bottom: 1px solid #C7D2FE;"
        "}"
    );
    QHBoxLayout* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(10, 6, 10, 6);
    headerLayout->setSpacing(6);

    // Year checkbox — checking it signals the controller
    m_yearCheckBox = new QCheckBox(QString::number(year));
    m_yearCheckBox->setStyleSheet(
        "QCheckBox { font-weight: 700; font-size: 14px; color: #1E3A5F; spacing: 6px; }"
        "QCheckBox::indicator { width: 16px; height: 16px; border-radius: 4px; border: 2px solid #6366F1; }"
        "QCheckBox::indicator:checked { background: #6366F1; }"
    );
    connect(m_yearCheckBox, &QCheckBox::toggled, this, [this](bool checked) {
        // Auto-expand when checked, collapse when unchecked
        setExpanded(checked);
        emit yearChecked(m_year, checked);
    });
    headerLayout->addWidget(m_yearCheckBox);

    // Collapse/expand arrow toggle
    m_toggle = new QToolButton();
    m_toggle->setCheckable(true);
    m_toggle->setChecked(false); // starts collapsed
    m_toggle->setArrowType(Qt::RightArrow);
    m_toggle->setToolButtonStyle(Qt::ToolButtonIconOnly);
    m_toggle->setFixedSize(22, 22);
    m_toggle->setStyleSheet(
        "QToolButton { background: transparent; border: none; }"
        "QToolButton:hover { background: #C7D2FE; border-radius: 4px; }"
    );
    connect(m_toggle, &QToolButton::toggled, this, &YearCard::toggle);
    headerLayout->addWidget(m_toggle);

    headerLayout->addStretch();

    // Q1-Q4 toggle buttons — only visible when expanded
    static const char* qLabels[4] = {"Q1", "Q2", "Q3", "Q4"};
    static const char* qTips[4] = {
        "Jan–Mar", "Apr–Jun", "Jul–Sep", "Oct–Dec"
    };
    for (int q = 0; q < 4; ++q) {
        m_quarterBtns[q] = new QPushButton(qLabels[q]);
        m_quarterBtns[q]->setCheckable(true);
        m_quarterBtns[q]->setFixedSize(34, 24);
        m_quarterBtns[q]->setToolTip(QString("%1 — %2").arg(qLabels[q]).arg(qTips[q]));
        m_quarterBtns[q]->setStyleSheet(
            "QPushButton { background: #F3F4F6; color: #374151; font-weight: 700; font-size: 10px; "
            "  border-radius: 4px; border: 1px solid #D1D5DB; }"
            "QPushButton:hover { background: #E0E7FF; border-color: #6366F1; }"
            "QPushButton:checked { background: #6366F1; color: white; border-color: #4F46E5; }"
        );
        const int quarter = q + 1;
        connect(m_quarterBtns[q], &QPushButton::toggled, this, [this, quarter](bool checked) {
            applyQuarter(quarter, checked);
            emit quarterToggled(m_year, quarter, checked);
        });
        m_quarterBtns[q]->setVisible(false); // shown only when expanded
        headerLayout->addWidget(m_quarterBtns[q]);
    }

    // Select/Deselect All buttons
    static const QString selStyle =
        "QPushButton { background: #4F46E5; color: white; font-weight: 600; font-size: 10px; "
        "  padding: 3px 7px; border-radius: 4px; }"
        "QPushButton:hover { background: #4338CA; }";
    static const QString deselStyle =
        "QPushButton { background: #F3F4F6; color: #374151; font-weight: 600; font-size: 10px; "
        "  padding: 3px 7px; border-radius: 4px; border: 1px solid #D1D5DB; }"
        "QPushButton:hover { background: #E5E7EB; }";

    QPushButton* btnAll = new QPushButton("All");
    btnAll->setFixedHeight(24);
    btnAll->setStyleSheet(selStyle);
    btnAll->setToolTip(QString("Select all months for %1").arg(year));
    btnAll->setVisible(false);
    connect(btnAll, &QPushButton::clicked, this, [this]() {
        for (PeriodRow* row : m_rows)
            for (QCheckBox* cb : row->monthCheckboxesInOrder())
                cb->setChecked(true);
    });
    headerLayout->addWidget(btnAll);

    QPushButton* btnNone = new QPushButton("None");
    btnNone->setFixedHeight(24);
    btnNone->setStyleSheet(deselStyle);
    btnNone->setToolTip(QString("Deselect all months for %1").arg(year));
    btnNone->setVisible(false);
    connect(btnNone, &QPushButton::clicked, this, [this]() {
        for (PeriodRow* row : m_rows)
            for (QCheckBox* cb : row->monthCheckboxesInOrder())
                cb->setChecked(false);
    });
    headerLayout->addWidget(btnNone);

    // Show quarter buttons + All/None when toggled
    connect(m_toggle, &QToolButton::toggled, this, [this, btnAll, btnNone](bool expanded) {
        for (int q = 0; q < 4; ++q)
            if (m_quarterBtns[q]) m_quarterBtns[q]->setVisible(expanded);
        btnAll->setVisible(expanded);
        btnNone->setVisible(expanded);
    });

    outerLayout->addWidget(header);

    // ── Collapsible content area ──────────────────────────────────────────────
    m_content = new QWidget();
    m_contentLayout = new QVBoxLayout(m_content);
    m_contentLayout->setContentsMargins(8, 8, 8, 8);
    m_contentLayout->setSpacing(6);
    m_content->setVisible(false); // starts collapsed
    outerLayout->addWidget(m_content);
}

void YearCard::clearRows()
{
    // Disconnect and delete all PeriodRow children safely
    for (int i = m_contentLayout->count() - 1; i >= 0; --i) {
        QLayoutItem* item = m_contentLayout->takeAt(i);
        if (!item) continue;
        if (QWidget* w = item->widget()) {
            w->disconnect();
            w->setParent(nullptr);
            delete w;
        }
        delete item;
    }
    m_rows.clear();
}

void YearCard::setExpanded(bool expanded)
{
    m_toggle->blockSignals(true);
    m_toggle->setChecked(expanded);
    m_toggle->blockSignals(false);
    toggle(expanded);
}

void YearCard::applyQuarter(int quarter, bool select)
{
    // Quarter month indices (0-based): Q1=0,1,2  Q2=3,4,5  Q3=6,7,8  Q4=9,10,11
    static const int qMonths[4][3] = {
        {0,1,2}, {3,4,5}, {6,7,8}, {9,10,11}
    };
    if (quarter < 1 || quarter > 4) return;
    const int* months = qMonths[quarter - 1];
    for (PeriodRow* row : m_rows) {
        QVector<QCheckBox*> cbs = row->monthCheckboxesInOrder();
        for (int i = 0; i < 3; ++i) {
            if (months[i] < cbs.size())
                cbs[months[i]]->setChecked(select);
        }
    }
}

// ── Public API ────────────────────────────────────────────────────────────

void YearCard::addRow(PeriodRow* row)
{
    m_rows.append(row);
    m_contentLayout->addWidget(row);
}

void YearCard::removeRow(PeriodRow* row)
{
    m_contentLayout->removeWidget(row);
    m_rows.removeOne(row);
}

bool YearCard::isEmpty() const
{
    for (const PeriodRow* row : m_rows) {
        if (!row->isEmpty()) return false;
    }
    return true;
}

// ── Private slot ─────────────────────────────────────────────────────────

void YearCard::toggle(bool checked)
{
    m_content->setVisible(checked);
    m_toggle->setArrowType(checked ? Qt::DownArrow : Qt::RightArrow);
}
