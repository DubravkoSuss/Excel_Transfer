#ifndef MAPPINGROW_H
#define MAPPINGROW_H

#include <QFrame>
#include <QCheckBox>
#include <QLineEdit>
#include <QLabel>
#include <QMap>
#include <QVector>
#include <QDialog>
#include <QTableWidget>
#include "../../core/mappingsmanager.h"

class MappingRow : public QFrame
{
    Q_OBJECT

public:
    explicit MappingRow(int index, const QString& month, int year,
                       const MappingEntry& entry, QWidget* parent = nullptr);
    ~MappingRow();

    int getIndex() const { return m_index; }
    QString getMonth() const { return m_month; }
    int getYear() const { return m_year; }
    MappingEntry getMapping() const { return m_entry; }
    void setIgnoredRows(const QSet<int>& rows) {
        m_entry.ignoredDestRows = rows;
    }
    QSet<int> ignoredRows() const { return m_entry.ignoredDestRows; }
    void setRowMap(const QMap<int, QVector<int>>& rowMap) { m_entry.rowMap = rowMap; }

    QString getSourceSheet() const;
    QString getSourceColumn() const;
    QVector<int> getSourceRows() const;
    QString getDestSheet() const;
    QString getDestColumn() const;
    QVector<int> getDestRows() const;

    void setSourceSheet(const QString& sheet);
    void setSourceColumn(const QString& col);
    void setSourceRows(const QVector<int>& rows);
    void setDestSheet(const QString& sheet);
    void setDestColumn(const QString& col);
    void setDestRows(const QVector<int>& rows);
    void setCopyFullSheet(bool enabled, const QString& customName = QString());
    bool isCopyFullSheet() const;
    QString getCustomSheetName() const;
    QString getInsertAfterSheet() const;
    void setDestSheetLabel(const QString& label);

    void openRowMapValidator();

    void setTransferStatus(bool success, const QString& message = QString());
    void clearTransferStatus();
    void setChecked(bool checked) { if (m_checkbox) m_checkbox->setChecked(checked); }
    bool isChecked() const { return m_checkbox && m_checkbox->isChecked(); }

signals:
    void runClicked(int index);
    void removeClicked(int index);
    void editRowsClicked(int index);
    void exportRowMapClicked(int index);
    void importRowMapClicked(int index);
    void ignoreRowsClicked(int index);
    void changed();

private slots:
    void onRunClicked();
    void onRemoveClicked();
    void onEditRowsClicked();
    void onExportRowMapClicked();
    void onImportRowMapClicked();

private:
    void buildUI();
    QString formatRowsPreview(const QVector<int>& rows, int maxShow = 5);

    int m_index;
    QString m_month;
    int m_year;
    MappingEntry m_entry;

    QCheckBox* m_checkbox;
    QLabel* m_badge;
    QLabel* m_mappingsLabel;
    QLabel* m_rowCountLabel;
    QLabel* m_statusLabel;
    QLineEdit* m_srcSheetEdit;
    QLineEdit* m_srcColumnEdit;
    QLabel* m_srcRowsPreview;
    QLineEdit* m_destSheetEdit;
    QLineEdit* m_destColumnEdit;
    QLabel* m_destRowsPreview;
    QLabel* m_sourceInfoLabel = nullptr;
    QCheckBox* m_copyFullSheetCheck = nullptr;
    QLineEdit* m_customSheetName = nullptr;
    QLineEdit* m_insertAfterSheet = nullptr;
};

class EditRowsDialog : public QDialog
{
    Q_OBJECT

public:
    EditRowsDialog(QWidget* parent, const QVector<int>& sourceRows,
                   const QVector<int>& destRows, const QString& srcSheet,
                   const QString& srcCol, const QString& destSheet, const QString& destCol);
    ~EditRowsDialog();

    QVector<int> getSourceRows() const { return m_sourceRows; }
    QVector<int> getDestRows() const { return m_destRows; }

private slots:
    void onAddRow();
    void onDeleteSelected();
    void onClearAll();
    void onSave();

private:
    void buildUI();

    QVector<int> m_sourceRows;
    QVector<int> m_destRows;

    QTableWidget* m_table;
    QLineEdit* m_srcSheetEdit;
    QLineEdit* m_srcColumnEdit;
    QLineEdit* m_destSheetEdit;
    QLineEdit* m_destColumnEdit;
};

#endif // MAPPINGROW_H
