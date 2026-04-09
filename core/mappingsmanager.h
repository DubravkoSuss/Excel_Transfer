#ifndef MAPPINGSManager_H
#define MAPPINGSManager_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QMap>
#include <QVector>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QFile>
#include <QDir>

struct MappingEntry {
    QString month;
    QString sourceSheetTemplate;
    QString sourceColumn;
    QVector<int> sourceRows;
    QString destSheet;
    QString destColumn;
    QVector<int> destRows;
    QMap<int, QVector<int>> rowMap;
    QVector<int> cumulativeRows;
    QString sourceFileType;
    QString sourceJson;
    QString sourcePath;
    bool copyFullSheet = false;
    QString customSheetName;
    QString insertAfterSheet; // optional: insert copied sheet after this sheet
    // User-selected rows to skip during transfer (dest row numbers)
    QSet<int> ignoredDestRows;

    // Optional traffic mott additions for YTD
    QString trafficSourceSheet;
    QString trafficSourceColumn;
    QVector<int> trafficSourceRows;   // rows in TRAFFIC mott sheet (dest of traffic_mott mapping)
    QMap<int,int> trafficPaxRowMap;   // destRow (MZLZ) → sourceRow (PAX Sheet1)
    QString trafficDestColumn;
};

struct PaxMappingEntry {
    QString month;
    QString sourceSheet;
    QString sourceColumn;
    QVector<int> sourceRows;
    QString destSheet;
    QString destColumn;
    QVector<int> destRows;
};

struct StaffMappingEntry {
    QString month;
    QString sourceSheet;   // may contain {year} template
    int sourceRow = 33;
    QString sourceColumn;
    QString destSheet;
    int destRow = 16;
    QString destColumn;
};

struct MonthMapping {
    QString month;
    QVector<MappingEntry> sources;
};

class MappingsManager : public QObject
{
    Q_OBJECT

public:
    explicit MappingsManager(QObject *parent = nullptr);
    ~MappingsManager();

    bool loadMappings(const QString& filePath);
    bool loadSapYtdMappings(const QString& filePath);
    bool saveMappings(const QString& filePath);
    bool loadPaxMappings(const QString& filePath);
    bool loadStaffMappings(const QString& filePath);
    bool loadBudgetRefiMappings(const QString& filePath);
    bool loadPaxTransferMappings(const QString& filePath);
    bool loadTrafficMottMappings(const QString& filePath);

    QVector<MappingEntry> getMappingsForMonthYear(const QString& month, int year);
    QVector<MappingEntry> getDynamicMappingsForMonthYear(const QString& month, int year);
    QVector<MappingEntry> getSapYtdMappingsForMonthYear(const QString& month, int year);
    QVector<MappingEntry> getPaxTransferMappingsForMonthYear(const QString& month, int year);
    QVector<MappingEntry> getTrafficMottMappingsForMonthYear(const QString& month, int year);
    PaxMappingEntry getPaxMappingForMonth(const QString& month);
    StaffMappingEntry getStaffMappingForMonth(const QString& month);
    
    void addMapping(const QString& month, const MappingEntry& entry);
    void removeMapping(const QString& month, int index);
    void updateMapping(const QString& month, int index, const MappingEntry& entry);
    
    QStringList getAvailableMonths() const;
    QStringList getSourceSheets() const;
    QStringList getDestSheets() const;
    
    static const QStringList MONTHS_LIST;
    static const QMap<QString, QString> MONTH_TO_NUM;
    static const QMap<int, QList<int>> QUARTERS;

    QString resolveSheetName(const QString& template_, int year, const QString& month = QString());

signals:
    void mappingsLoaded(bool success, const QString& error);
    void mappingsSaved(bool success, const QString& error);

private:
    QMap<QString, MonthMapping> m_mappings;
    QMap<QString, PaxMappingEntry> m_paxMappings;
    QMap<QString, StaffMappingEntry> m_staffMappings;
    QMap<QString, QVector<MappingEntry>> m_budgetRefiMappings;
    QMap<QString, QVector<MappingEntry>> m_paxTransferMappings;
    QMap<QString, QVector<MappingEntry>> m_trafficMottMappings;
    QMap<QString, MonthMapping> m_sapYtdMappings;
    QString m_currentFilePath;
    
    bool parseJsonFile(const QString& filePath);
    bool writeJsonFile(const QString& filePath);
    QString cleanSheetName(const QString& sheetName) const;
};

#endif // MAPPINGSManager_H
