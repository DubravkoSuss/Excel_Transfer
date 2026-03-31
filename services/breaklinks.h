#ifndef BREAKLINKS_H
#define BREAKLINKS_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QMap>
#include <QSet>
#include <QVector>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>
#include <QFile>
#include <QTemporaryFile>
#include <QDir>
#include <QRegularExpression>

class BreakLinks : public QObject
{
    Q_OBJECT

public:
    explicit BreakLinks(QObject *parent = nullptr);
    ~BreakLinks();

    bool breakExternalLinks(const QString& filePath, const QString& valuesFilePath = QString());
    
    struct Result {
        bool success;
        QString error;
        int filesModified;
    };

signals:
    void progress(int current, int total, const QString& message);
    void complete(const Result& result);

private:
    struct ExtLinkInfo {
        QString partName;
        QString relsName;
    };
    
    QVector<ExtLinkInfo> findExternalLinks(const QString& zipPath);
    bool processWorkbookXml(QByteArray& data);
    bool processWorkbookRels(QByteArray& data);
    bool processContentTypes(QByteArray& data);
    bool processWorksheetXml(QByteArray& data, int sheetIndex, const QString& valuesZipPath = QString());
    
    QStringList getZipContents(const QString& zipPath);
    QByteArray readZipEntry(const QString& zipPath, const QString& entryName);
    bool writeZipEntry(const QString& zipPath, const QString& entryName, const QByteArray& data);
    
    QString getSheetIndexFromPath(const QString& path);
    QByteArray getCachedValue(const QString& valuesRootPath, int sheetIndex, const QString& cellRef);
    QMap<int, QMap<QString, QByteArray>> loadCachedSheetValues(const QString& valuesRootPath, int sheetIndex);
    
    static const QString NS_SML;
    static const QString NS_REL;
    static const QString NS_CT;
    static const QString EXT_LINK_TYPE;
    static const QString EXT_LINK_CT;

    QString m_cachedValuesRoot;
    QMap<int, QMap<QString, QByteArray>> m_cachedValues;
};

#endif // BREAKLINKS_H
