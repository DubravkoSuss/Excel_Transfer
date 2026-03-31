#ifndef INDIVIDUALTRANSFERTAB_H
#define INDIVIDUALTRANSFERTAB_H

#include <QWidget>

class IndividualTransferPanel;
class MainWindow;

class IndividualTransferTab : public QWidget
{
    Q_OBJECT

public:
    explicit IndividualTransferTab(MainWindow* mainWindow, QWidget* parent = nullptr);
    IndividualTransferPanel* panel() const { return m_panel; }

private:
    IndividualTransferPanel* m_panel = nullptr;
};

#endif // INDIVIDUALTRANSFERTAB_H
