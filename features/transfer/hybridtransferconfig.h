#ifndef HYBRIDTRANSFERCONFIG_H
#define HYBRIDTRANSFERCONFIG_H

#include <QString>
#include <QVector>
#include <QPair>

struct HybridTransferConfig {
    // Periods assigned to Execute All (normal cell-by-cell transfer)
    QVector<QPair<QString, int>> executeAllPeriods;   // {month, year}
    
    // Periods assigned to Execute RT (rolling transfer)
    QVector<QPair<QString, int>> executeRTPeriods;     // {month, year}
    
    // Execution order: true = Execute All first, then RT
    // false = RT first, then Execute All
    bool executeAllFirst = true;
    
    bool hasExecuteAll() const { return !executeAllPeriods.isEmpty(); }
    bool hasExecuteRT() const { return !executeRTPeriods.isEmpty(); }
    bool isEmpty() const { return executeAllPeriods.isEmpty() && executeRTPeriods.isEmpty(); }
    
    int totalPeriods() const {
        return executeAllPeriods.size() + executeRTPeriods.size();
    }
};

#endif // HYBRIDTRANSFERCONFIG_H
