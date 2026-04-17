#ifndef ROLLOVERWORKER_H
#define ROLLOVERWORKER_H

#include <QThread>
#include <QString>
#include <QStringList>
#include <QMap>

struct RolloverConfig {
    QString sourceFile;  // The loaded 2026 file
    QString destFile;    // The new 2027 file to save
    int     targetYear;  // E.g. 2027
    int     startRow = 5;
    int     endRow   = 300;
};

class ExcelHandler;

class RolloverWorker : public QThread
{
    Q_OBJECT

public:
    explicit RolloverWorker(const RolloverConfig& config, QObject* parent = nullptr);
    ~RolloverWorker() override;

signals:
    void progress(int current, int total, const QString& message);
    void finished(bool success, const QString& message);

protected:
    void run() override;

private:
    bool isStipulatedFormula(const QString& formula) const;

    RolloverConfig m_config;
    QString m_fileKey;
    volatile bool m_stopRequested = false;
};

#endif // ROLLOVERWORKER_H
