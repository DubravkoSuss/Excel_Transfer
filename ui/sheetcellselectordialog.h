// Include necessary Qt headers at the beginning of sheetcellselectordialog.h
#include <QDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QComboBox>
#include <QSpinBox>
#include <QPushButton>
#include <QDialogButtonBox>

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

    void setSheetNames(const QStringList& sheetNames);
    void setCellSelection(const CellSelection& selection);
    CellSelection getCellSelection() const;

signals:
    void cellSelected(const CellSelection& selection);

private slots:
    void onSelectClicked();
    void onCancelClicked();
    void onSheetChanged(int index);
    void onColumnChanged(const QString& text);
    void onRowChanged(int value);

private:
    void setupUI();
    void updateFields();
    QString columnNumberToLetter(int column) const;
    int columnLetterToNumber(const QString& column) const;

    QComboBox* m_sheetCombo;
    QLineEdit* m_columnEdit;
    QSpinBox* m_rowSpinBox;

    QPushButton* m_selectBtn;
    QPushButton* m_cancelBtn;

    CellSelection m_currentSelection;
};