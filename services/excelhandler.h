#ifndef EXCELHANDLER_H
#define EXCELHANDLER_H

#include <QObject>
#include <QString>
#include <QMap>
#include <QVariant>
#include <QVector>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QFile>
#include <QDir>
#include <QThread>
#include <QMutex>
#include <QReadWriteLock>
#include <QUuid>

struct CellData {
    QString value;
    QString formula;
    QString dataType;
    QString numberFormat;
    int row;
    int col;
};

struct SheetData {
    QString name;
    QMap<QString, CellData> cells;
    int maxRow;
    int maxCol;
};

struct WorkbookData {
    QString filePath;
    QStringList sheetNames;
    QMap<QString, SheetData> sheets;          // lazily populated on first access
    QMap<QString, QString> sheetPathMap;       // sheetName → "xl/worksheets/sheetN.xml"
    QMap<QString, QByteArray> rawSheetOverrides; // raw XML overrides for full-sheet copy
    QMap<QString, QSet<QString>> dirtyCells;     // sheetName → set of cellRefs that were explicitly written
    QMap<QString, QVector<int>> highlightSourceRows; // sheetName → source row numbers (dark blue)
    QMap<QString, QString> insertAfterSheet; // sheetName → insert after (sheet order)
    QMap<QString, QByteArray> cachedZipEntries;  // ALL files from ZIP (for save target workbooks)
    QVector<QString> sharedStrings;            // loaded upfront, shared across all sheets
    QSet<QString> modifiedSheets;             // track which sheets were written to
    bool isValid = false;
    bool isSaveTarget = false;                 // if true, cache all ZIP entries on load
};

struct PeriodKey {
    QString month;
    int year;
    QString type;
    
    bool operator<(const PeriodKey& other) const {
        if (year != other.year) return year < other.year;
        if (month != other.month) return month < other.month;
        return type < other.type;
    }
};

class ExcelHandler : public QObject
{
    Q_OBJECT

public:
    static const QMap<QString, QString> MONTH_TO_NUM;

    explicit ExcelHandler(QObject *parent = nullptr);
    ~ExcelHandler();

    bool loadWorkbook(const QString& filePath, const QString& key,
                      const QSet<QString>& sheetsNeeded = QSet<QString>());
    bool saveWorkbook(const QString& key, const QString& outputPath = QString());
    void unloadWorkbook(const QString& key);
    void unloadAll();          // Clears ALL cached workbooks from memory

    // COM-based recalculation: opens the file in Excel, accepts any repair dialog,
    // forces full recalculation, saves and closes. Call BEFORE loadWorkbook() so the
    // cached formula results on disk are correct when OpenXML reads them.
    // Returns true on success. Requires Excel installed on the machine.
    static bool recalcWithCOM(const QString& filePath, QString* errorOut = nullptr);
    bool isLoaded(const QString& key);
    
    QStringList getSheetNames(const QString& key);
    QVariant getCellValue(const QString& key, const QString& sheetName, int row, int col);
    bool setCellValue(const QString& key, const QString& sheetName, int row, int col, const QVariant& value);
    bool setCellFormula(const QString& key, const QString& sheetName, int row, int col, const QString& formula);
    
    bool copyData(const QString& srcKey, const QString& srcSheet, const QString& srcCol, 
                  const QVector<int>& srcRows, const QString& destKey, const QString& destSheet,
                  const QString& destCol, const QVector<int>& destRows);
    
    bool copyFullSheet(const QString& srcKey, const QString& srcSheet,
                       const QString& destKey, const QString& newSheetName,
                       const QVector<int>& highlightRows = QVector<int>());
    bool renameSheet(const QString& key, const QString& oldName, const QString& newName);
    void resetOverrides(const QString& key);
    void setInsertAfter(const QString& key, const QString& sheetName, const QString& insertAfter);
    
    QString findCostControlFile(const QString& basePath, const QString& month, int year);
    QString findSAPFile(const QString& basePath, const QString& month, int year);
    QString findSapYtdFile(const QString& basePath, const QString& month, int year);

    QString columnToLetter(int col);
    int letterToColumn(const QString& letter);

    // Static versions for use in free functions
    static QString staticColumnToLetter(int col);
    static int staticLetterToColumn(const QString& letter);

signals:
    void loadProgress(int current, int total, const QString& message);
    void loadComplete(const QString& key, bool success, const QString& error);
    void saveProgress(int current, int total, const QString& message);
    void saveComplete(bool success, const QString& error);

public:
    // Transfer with division option
    int transferData(const QString& srcKey, const QString& srcSheet, const QString& srcCol,
                    const QVector<int>& srcRows, const QString& destKey, const QString& destSheet,
                    const QString& destCol, const QVector<int>& destRows,
                    const QString& sourceFileType, bool divideBy1000 = false);
    
    // Load workbook with data_only mode (for reading cached values)
    bool loadWorkbookDataOnly(const QString& filePath, const QString& key);
    
    // Find PAX and Staff files
    QString findPaxFile(const QString& basePath, const QString& month, int year);
    QString findStaffFile(const QString& basePath, int year);

private:
    QMap<QString, WorkbookData> m_workbooks;
    mutable QReadWriteLock m_lock;
    
    bool loadOpenXML(const QString& filePath, WorkbookData& wb,
                     const QSet<QString>& sheetsNeeded = QSet<QString>());  // loads metadata + optional pre-parse
    SheetData loadSheetLazy(const WorkbookData& wb, const QString& sheetName); // loads one sheet on demand
    bool saveOpenXML(const QString& filePath, const WorkbookData& wb);
    SheetData& getSheet(const QString& key, const QString& sheetName); // lazy accessor (write lock required)
};

#endif // EXCELHANDLER_H
