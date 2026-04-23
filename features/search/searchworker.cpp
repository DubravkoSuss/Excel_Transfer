#include "searchworker.h"
#include "excelsearchservice.h"
#include "../../services/excelhandler.h"
#include <QDateTime>

SearchWorker::SearchWorker(ExcelHandler* handler,
                            const SearchRequest& request,
                            QObject* parent)
    : QThread(parent), m_handler(handler), m_request(request)
{
}

SearchWorker::~SearchWorker()
{
}

void SearchWorker::requestStop()
{
    m_stopRequested.storeRelaxed(1);
}

void SearchWorker::run()
{
    // Create a local service (touched only from this thread)
    ExcelSearchService svc(m_handler, nullptr);

    connect(&svc, &ExcelSearchService::progress,
            this, &SearchWorker::progress,
            Qt::DirectConnection);

    // ── Determine whether the file is already loaded ──────────────────────
    // The tab sets fileKey to "search_preloaded_<timestamp>" when it has
    // already loaded the workbook itself. In that case we skip loading here.
    // Otherwise fileKey holds the raw file path and we load it now.

    bool preloaded = m_request.fileKey.startsWith("search_preloaded_");

    if (!preloaded) {
        // fileKey is a file path — load it under a unique key
        QString filePath = m_request.fileKey;
        QString loadKey  = "search_worker_" +
                           QString::number(QDateTime::currentMSecsSinceEpoch());

        bool loaded = m_handler->loadWorkbook(filePath, loadKey);
        if (!loaded) {
            emit finished(false, "Failed to load workbook: " + filePath, {});
            return;
        }
        m_request.fileKey = loadKey;
    }
    // else: m_request.fileKey is already a valid key inside m_handler — use as-is

    QVector<SearchMatch> results = svc.search(m_request, &m_stopRequested);

    if (m_stopRequested.loadRelaxed()) {
        emit finished(false, "Search cancelled by user.", results);
        return;
    }

    emit finished(true,
                  QString("Search complete. Found %1 match(es).").arg(results.size()),
                  results);
}