#pragma once
#include <QString>
#include <QVector>
#include <QStringList>

struct FillAllFileEntry {
    QString month;          // "January", "February", ...
    int     monthNumber;    // 1-based: Jan=1, Dec=12
    QString transferType;   // "sap", "budget_refi", "traffic_mott", "staff", "pax_transfer", "sap_ytd"
    QString sourceFilePath; // full path to source file (empty if not found)
    bool    found = false;  // true if file exists on disk
    QString statusMessage;  // "OK" or "File not found: ..."
};

struct FillAllScanResult {
    int     year = 0;
    int     targetMonth = 0;         // e.g. 8 for August (1-based)
    QString destFilePath;            // cost_control file for the target month
    QString destFolder;              // folder of the target month
    QVector<FillAllFileEntry> entries;

    int foundCount() const {
        int c = 0;
        for (const auto& e : entries) if (e.found) c++;
        return c;
    }
    int missingCount() const {
        return entries.size() - foundCount();
    }
};

struct FillAllResult {
    int totalTransfers   = 0;
    int successCount     = 0;
    int failCount        = 0;
    int cellsTransferred = 0;
    QStringList warnings;   // missing files (skipped)
    QStringList errors;     // transfer failures
};
