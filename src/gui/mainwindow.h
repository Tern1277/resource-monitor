// mainwindow.h — головне вікно програми "Монітор ресурсів ПК".
#pragma once

#include "resourcemonitor.h"

#include <QMainWindow>

class QAction;
class QComboBox;
class QLabel;
class QProgressBar;
class QPushButton;
class QTableWidget;
class HistoryChart;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

private slots:
    void onUpdated(const rm::Snapshot &snapshot);
    void togglePause();
    void refreshNow();
    void onIntervalChanged(int index);
    void saveReport();
    void showHelp();
    void showAbout();

private:
    void buildUi();
    void buildMenu();
    void updateDiskTable(const QList<rm::DiskInfo> &disks);
    bool confirmOverwrite(const QString &path);
    void showInfo(const QString &title, const QString &text, bool isError = false);
    static void applyLevel(QProgressBar *bar, double percent, bool valid = true,
                           const QString &placeholder = QStringLiteral("немає даних"));

    rm::ResourceMonitor m_monitor;

    QProgressBar *m_cpuBar = nullptr;
    QLabel *m_cpuLabel = nullptr;
    QLabel *m_coresLabel = nullptr;
    QProgressBar *m_memoryBar = nullptr;
    QLabel *m_memoryLabel = nullptr;
    QProgressBar *m_swapBar = nullptr;
    QLabel *m_swapLabel = nullptr;
    HistoryChart *m_chart = nullptr;
    QTableWidget *m_diskTable = nullptr;
    QComboBox *m_intervalBox = nullptr;
    QPushButton *m_pauseButton = nullptr;
    QLabel *m_updatedLabel = nullptr;

    QAction *m_pauseAction = nullptr;
};
