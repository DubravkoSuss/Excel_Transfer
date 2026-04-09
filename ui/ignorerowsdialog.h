#pragma once
#include <QDialog>
#include <QSet>
#include <QTableWidget>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QCheckBox>
#include <QLineEdit>
#include "../core/mappingsmanager.h"

class ExcelHandler;

class IgnoreRowsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit IgnoreRowsDialog(const MappingEntry& entry,
                               ExcelHandler* handler,
                               const QString& destWorkbookKey,
                               QWidget* parent = nullptr);

    // Returns the updated set of ignored dest rows
    QSet<int> ignoredDestRows() const { return m_ignoredRows; }

private slots:
    void onSelectAll();
    void onSelectNone();
    void onAccept();
    void onFilter(const QString& text);

private:
    void setupUI();
    void populateTable();

    MappingEntry    m_entry;
    ExcelHandler*   m_handler;
    QString         m_destKey;
    QSet<int>       m_ignoredRows;

    QTableWidget*   m_table;
    QLabel*         m_statusLabel;
    QLineEdit*      m_filterEdit;
};
