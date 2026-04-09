#include "ignorerowsdialog.h"
#include "../services/excelhandler.h"
#include <QHeaderView>
#include <QGuiApplication>
#include <QScreen>
#include <QScrollBar>

IgnoreRowsDialog::IgnoreRowsDialog(const MappingEntry& entry,
                                    ExcelHandler* handler,
                                    const QString& destWorkbookKey,
                                    QWidget* parent)
    : QDialog(parent)
    , m_entry(entry)
    , m_handler(handler)
    , m_destKey(destWorkbookKey)
    , m_ignoredRows(entry.ignoredDestRows)
{
    setupUI();
    populateTable();
}

void IgnoreRowsDialog::setupUI()
{
    setWindowTitle(QString("Ignore Rows — %1 → %2")
                       .arg(m_entry.sourceSheetTemplate, m_entry.destSheet));
    setMinimumSize(700, 500);
    setModal(true);

    // Center on screen
    QScreen* screen = QGuiApplication::primaryScreen();
    if (screen) {
        QRect sg = screen->availableGeometry();
        resize(qMin(900, sg.width() - 100), qMin(600, sg.height() - 100));
        move(sg.center() - rect().center());
    }

    QVBoxLayout* main = new QVBoxLayout(this);
    main->setContentsMargins(16, 16, 16, 16);
    main->setSpacing(10);

    // Title
    QLabel* title = new QLabel(QString("Select rows to <b>skip</b> during transfer for:<br>"
                                       "<span style='color:#1E40AF'>%1 col %2 → %3 col %4</span>")
                                   .arg(m_entry.sourceSheetTemplate, m_entry.sourceColumn,
                                        m_entry.destSheet, m_entry.destColumn));
    title->setStyleSheet("font-size: 13px; padding: 4px;");
    main->addWidget(title);

    // Filter + Select All/None
    QHBoxLayout* toolBar = new QHBoxLayout();
    QLabel* filterLabel = new QLabel("Filter:");
    m_filterEdit = new QLineEdit();
    m_filterEdit->setPlaceholderText("Filter by row number or value...");
    m_filterEdit->setStyleSheet("padding: 6px; border: 1px solid #D1D5DB; border-radius: 4px;");
    connect(m_filterEdit, &QLineEdit::textChanged, this, &IgnoreRowsDialog::onFilter);

    QPushButton* allBtn = new QPushButton("Ignore All");
    allBtn->setStyleSheet("padding: 6px 12px; background: #FEF3C7; color: #92400E; "
                          "border: 1px solid #FCD34D; border-radius: 4px; font-weight: 600;");
    connect(allBtn, &QPushButton::clicked, this, &IgnoreRowsDialog::onSelectAll);

    QPushButton* noneBtn = new QPushButton("Ignore None");
    noneBtn->setStyleSheet("padding: 6px 12px; background: #D1FAE5; color: #065F46; "
                           "border: 1px solid #6EE7B7; border-radius: 4px; font-weight: 600;");
    connect(noneBtn, &QPushButton::clicked, this, &IgnoreRowsDialog::onSelectNone);

    toolBar->addWidget(filterLabel);
    toolBar->addWidget(m_filterEdit, 1);
    toolBar->addWidget(allBtn);
    toolBar->addWidget(noneBtn);
    main->addLayout(toolBar);

    // Table: columns = [Ignore, Dest Row, Dest Col, Src Row(s), Current Dest Value]
    m_table = new QTableWidget(this);
    m_table->setColumnCount(5);
    m_table->setHorizontalHeaderLabels({"Skip", "Dest Row", "Dest Col", "Src Row(s)", "Current Value"});
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
    m_table->setColumnWidth(0, 50);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Fixed);
    m_table->setColumnWidth(1, 80);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Fixed);
    m_table->setColumnWidth(2, 80);
    m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionMode(QAbstractItemView::NoSelection);
    m_table->setAlternatingRowColors(true);
    m_table->verticalHeader()->setVisible(false);
    m_table->setStyleSheet(
        "QTableWidget { border: 1px solid #D1D5DB; font-size: 12px; }"
        "QTableWidget::item { padding: 4px; }"
    );
    main->addWidget(m_table, 1);

    // Status label
    m_statusLabel = new QLabel();
    m_statusLabel->setStyleSheet("color: #6B7280; font-size: 11px;");
    main->addWidget(m_statusLabel);

    // Buttons
    QHBoxLayout* btnLayout = new QHBoxLayout();
    btnLayout->addStretch();

    QPushButton* applyBtn = new QPushButton("✓  Apply");
    applyBtn->setStyleSheet("background: #059669; color: white; font-weight: 600; "
                            "padding: 10px 24px; border-radius: 6px;");
    connect(applyBtn, &QPushButton::clicked, this, &IgnoreRowsDialog::onAccept);

    QPushButton* cancelBtn = new QPushButton("Cancel");
    cancelBtn->setStyleSheet("background: #F3F4F6; color: #374151; "
                             "padding: 10px 20px; border-radius: 6px;");
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);

    btnLayout->addWidget(applyBtn);
    btnLayout->addWidget(cancelBtn);
    main->addLayout(btnLayout);
}

void IgnoreRowsDialog::populateTable()
{
    // Build sorted list of rowMap entries
    QList<int> destRows = m_entry.rowMap.keys();
    std::sort(destRows.begin(), destRows.end());

    m_table->setRowCount(destRows.size());

    const QString destCol = m_entry.destColumn;
    const int destColIdx  = m_handler ? m_handler->letterToColumn(destCol) : 0;

    for (int i = 0; i < destRows.size(); ++i) {
        int destRow = destRows[i];
        const QVector<int>& srcRows = m_entry.rowMap[destRow];

        // Checkbox (col 0)
        QCheckBox* cb = new QCheckBox();
        cb->setChecked(m_ignoredRows.contains(destRow));
        cb->setStyleSheet("margin-left: 14px;");
        int capturedRow = destRow;
        connect(cb, &QCheckBox::toggled, this, [this, capturedRow](bool checked) {
            if (checked) m_ignoredRows.insert(capturedRow);
            else         m_ignoredRows.remove(capturedRow);
            // Update status
            m_statusLabel->setText(QString("%1 rows ignored out of %2")
                                       .arg(m_ignoredRows.size())
                                       .arg(m_entry.rowMap.size()));
        });
        m_table->setCellWidget(i, 0, cb);

        // Dest Row (col 1)
        auto* destRowItem = new QTableWidgetItem(QString::number(destRow));
        destRowItem->setTextAlignment(Qt::AlignCenter);
        m_table->setItem(i, 1, destRowItem);

        // Dest Col (col 2)
        auto* destColItem = new QTableWidgetItem(destCol);
        destColItem->setTextAlignment(Qt::AlignCenter);
        m_table->setItem(i, 2, destColItem);

        // Src Row(s) (col 3)
        QStringList srcList;
        for (int sr : srcRows) srcList << QString::number(sr);
        m_table->setItem(i, 3, new QTableWidgetItem(srcList.join(", ")));

        // Current dest value (col 4) — lazy loaded from ExcelHandler
        QString currentVal = "—";
        if (m_handler && !m_destKey.isEmpty() && destColIdx > 0) {
            QVariant v = m_handler->getCellValue(m_destKey, m_entry.destSheet, destRow, destColIdx);
            if (v.isValid() && !v.toString().isEmpty())
                currentVal = v.toString();
        }
        auto* valItem = new QTableWidgetItem(currentVal);
        valItem->setForeground(QColor("#6B7280"));
        m_table->setItem(i, 4, valItem);
    }

    m_statusLabel->setText(QString("%1 rows ignored out of %2")
                               .arg(m_ignoredRows.size())
                               .arg(m_entry.rowMap.size()));
}

void IgnoreRowsDialog::onSelectAll()
{
    for (int i = 0; i < m_table->rowCount(); ++i) {
        if (!m_table->isRowHidden(i)) {
            if (auto* cb = qobject_cast<QCheckBox*>(m_table->cellWidget(i, 0)))
                cb->setChecked(true);
        }
    }
}

void IgnoreRowsDialog::onSelectNone()
{
    for (int i = 0; i < m_table->rowCount(); ++i) {
        if (auto* cb = qobject_cast<QCheckBox*>(m_table->cellWidget(i, 0)))
            cb->setChecked(false);
    }
}

void IgnoreRowsDialog::onAccept()
{
    accept();
}

void IgnoreRowsDialog::onFilter(const QString& text)
{
    for (int i = 0; i < m_table->rowCount(); ++i) {
        bool show = true;
        if (!text.isEmpty()) {
            bool match = false;
            for (int c = 1; c < m_table->columnCount(); ++c) {
                if (auto* item = m_table->item(i, c)) {
                    if (item->text().contains(text, Qt::CaseInsensitive)) {
                        match = true;
                        break;
                    }
                }
            }
            show = match;
        }
        m_table->setRowHidden(i, !show);
    }
}
