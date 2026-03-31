#include "individualtransfertab.h"
#include "../features/transfer/individualtransferpanel.h"
#include "mainwindow.h"
#include <QVBoxLayout>

IndividualTransferTab::IndividualTransferTab(MainWindow* mainWindow, QWidget* parent)
    : QWidget(parent)
{
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(16);

    m_panel = new IndividualTransferPanel(this);
    layout->addWidget(m_panel);

    // Connect panel signals → MainWindow slots
    connect(m_panel, &IndividualTransferPanel::browseSourceClicked,
            mainWindow, &MainWindow::onIndividualBrowseSource);
    connect(m_panel, &IndividualTransferPanel::browseDestClicked,
            mainWindow, &MainWindow::onIndividualBrowseDest);
    connect(m_panel, &IndividualTransferPanel::selectSourceCellClicked,
            mainWindow, &MainWindow::onIndividualSelectSourceCell);
    connect(m_panel, &IndividualTransferPanel::selectDestCellClicked,
            mainWindow, &MainWindow::onIndividualSelectDestCell);
    connect(m_panel, &IndividualTransferPanel::transferClicked,
            mainWindow, &MainWindow::onIndividualTransfer);
}
