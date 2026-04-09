#pragma once
#include <QDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QComboBox>
#include <QSpinBox>
#include <QPushButton>
#include <QTableWidget>
#include <QHeaderView>
#include <QDialogButtonBox>
#include <QSplitter>

class ExcelHandler;

class SheetCellSelectorDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SheetCellSelectorDialog(QWidget *parent = nullptr);
    ~SheetCellSelectorDialog() override;

    struct CellSelection {
        QString sheetName;
        QString column;
        int row;
    };

    // Set sheet names (without data preview)
    void setSheetNames(const QStringList& sheetNames);

    // Set ExcelHandler + workbook key for data preview
    void setExcelHandler(ExcelHandler* handler, const QString& workbookKey);

    void setCellSelection(const CellSelection& selection);
    CellSelection getCellSelection() const;
    QList<CellSelection> getCellSelections() const;

signals:
    void cellSelected(const CellSelection& selection);

private slots:
    void onSelectClicked();
    void onCancelClicked();
    void onSheetChanged(int index);
    void onColumnChanged(const QString& text);
    void onRowChanged(int value);
    void onTableCellClicked(int row, int col);

private:
    void setupUI();
    void updateFields();
    void populateTable();
    QString columnNumberToLetter(int column) const;
    int columnLetterToNumber(const QString& column) const;

    QComboBox*    m_sheetCombo;
    QLineEdit*    m_columnEdit;
    QSpinBox*     m_rowSpinBox;
    QTableWidget* m_tableWidget;
    QLabel*       m_selectedCellLabel;

    QPushButton* m_selectBtn;
    QPushButton* m_cancelBtn;

    CellSelection  m_currentSelection;
    ExcelHandler*  m_handler    = nullptr;
    QString        m_workbookKey;

    static constexpr int PREVIEW_ROWS = 50;
    static constexpr int PREVIEW_COLS = 26; // A–Z
};