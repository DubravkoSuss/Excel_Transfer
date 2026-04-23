#ifndef SEARCHWORKER_H
#define SEARCHWORKER_H

#include <QThread>
#include <QAtomicInt>
#include "excelsearchservice.h"

class ExcelHandler;

class SearchWorker : public QThread
{
    Q_OBJECT

public:
    explicit SearchWorker(ExcelHandler* handler,
                          const SearchRequest& request,
                          QObject* parent = nullptr);
    ~SearchWorker() override;

    void requestStop();

signals:
    void progress(int current, int total, const QString& message);
    void finished(bool success, const QString& message,
                  QVector<SearchMatch> results);

protected:
    void run() override;

private:
    ExcelHandler*   m_handler;
    SearchRequest   m_request;
    QAtomicInt      m_stopRequested { 0 };
};

#endif // SEARCHWORKER_H