// mainwindow.cpp — побудова інтерфейсу та обробка подій головного вікна.
#include "mainwindow.h"

#include "formatting.h"
#include "historychart.h"

#include <QAction>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QSaveFile>
#include <QStatusBar>
#include <QSysInfo>
#include <QTableWidget>
#include <QVBoxLayout>

using namespace rm;

namespace {

// Колонки таблиці дисків.
enum DiskColumn {
    ColMount,
    ColDevice,
    ColType,
    ColTotal,
    ColUsed,
    ColFree,
    ColFill,
    ColCount
};

/// Колір смуги заповнення залежно від рівня навантаження.
QString colorFor(LoadLevel level)
{
    switch (level) {
    case LoadLevel::Normal:
        return QStringLiteral("#66bb6a");   // зелений
    case LoadLevel::Warning:
        return QStringLiteral("#ffa726");   // помаранчевий
    case LoadLevel::Critical:
        return QStringLiteral("#ef5350");   // червоний
    }
    return QStringLiteral("#9e9e9e");
}

QProgressBar *makeBar()
{
    auto *bar = new QProgressBar;
    bar->setRange(0, 1000);   // крок 0,1 %
    bar->setMinimumHeight(24);
    bar->setAlignment(Qt::AlignCenter);
    return bar;
}

}   // namespace

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("Монітор ресурсів ПК"));
    resize(960, 740);
    setMinimumSize(780, 620);

    buildUi();
    buildMenu();

    connect(&m_monitor, &ResourceMonitor::updated, this, &MainWindow::onUpdated);
    m_monitor.sampleNow();   // одразу показати пам'ять і диски
    m_monitor.start();
}

void MainWindow::buildUi()
{
    auto *central = new QWidget(this);
    auto *root = new QVBoxLayout(central);
    root->setContentsMargins(14, 10, 14, 6);

    // Заголовок і відомості про систему.
    auto *title = new QLabel(QStringLiteral("Монітор ресурсів ПК"));
    QFont titleFont = title->font();
    titleFont.setPointSizeF(titleFont.pointSizeF() * 1.5);
    titleFont.setBold(true);
    title->setFont(titleFont);
    auto *systemLabel =
        new QLabel(QStringLiteral("Система: %1 · ядро Linux %2 · вузол %3")
                       .arg(QSysInfo::prettyProductName(), QSysInfo::kernelVersion(),
                            QSysInfo::machineHostName()));
    systemLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    root->addWidget(title);
    root->addWidget(systemLabel);

    // Панель керування.
    auto *controls = new QHBoxLayout;
    auto *intervalLabel = new QLabel(QStringLiteral("Інтервал оновлення:"));
    m_intervalBox = new QComboBox;
    m_intervalBox->addItem(QStringLiteral("1 с"), 1000);
    m_intervalBox->addItem(QStringLiteral("2 с"), 2000);
    m_intervalBox->addItem(QStringLiteral("5 с"), 5000);
    m_intervalBox->addItem(QStringLiteral("10 с"), 10000);
    intervalLabel->setBuddy(m_intervalBox);
    m_pauseButton = new QPushButton(QStringLiteral("Пауза"));
    auto *refreshButton = new QPushButton(QStringLiteral("Оновити зараз"));
    auto *saveButton = new QPushButton(QStringLiteral("Зберегти звіт…"));
    controls->addWidget(intervalLabel);
    controls->addWidget(m_intervalBox);
    controls->addSpacing(12);
    controls->addWidget(m_pauseButton);
    controls->addWidget(refreshButton);
    controls->addWidget(saveButton);
    controls->addStretch();
    root->addLayout(controls);

    connect(m_intervalBox, qOverload<int>(&QComboBox::currentIndexChanged), this,
            &MainWindow::onIntervalChanged);
    connect(m_pauseButton, &QPushButton::clicked, this, &MainWindow::togglePause);
    connect(refreshButton, &QPushButton::clicked, this, &MainWindow::refreshNow);
    connect(saveButton, &QPushButton::clicked, this, &MainWindow::saveReport);

    // Блок "Процесор і пам'ять": смуги, підписи та графік історії.
    auto *usageBox = new QGroupBox(QStringLiteral("Процесор і оперативна пам'ять"));
    auto *grid = new QGridLayout(usageBox);
    m_cpuBar = makeBar();
    m_memoryBar = makeBar();
    m_swapBar = makeBar();
    m_cpuLabel = new QLabel;
    m_coresLabel = new QLabel;
    m_coresLabel->setWordWrap(true);
    m_memoryLabel = new QLabel;
    m_swapLabel = new QLabel;
    grid->addWidget(new QLabel(QStringLiteral("Процесор:")), 0, 0);
    grid->addWidget(m_cpuBar, 0, 1);
    grid->addWidget(m_cpuLabel, 0, 2);
    grid->addWidget(m_coresLabel, 1, 1, 1, 2);
    grid->addWidget(new QLabel(QStringLiteral("Пам'ять (RAM):")), 2, 0);
    grid->addWidget(m_memoryBar, 2, 1);
    grid->addWidget(m_memoryLabel, 2, 2);
    grid->addWidget(new QLabel(QStringLiteral("Файл підкачки:")), 3, 0);
    grid->addWidget(m_swapBar, 3, 1);
    grid->addWidget(m_swapLabel, 3, 2);
    grid->setColumnStretch(1, 3);
    grid->setColumnStretch(2, 2);
    m_chart = new HistoryChart;
    grid->addWidget(m_chart, 4, 0, 1, 3);
    root->addWidget(usageBox, 3);

    // Блок "Диски": таблиця файлових систем.
    auto *diskBox = new QGroupBox(QStringLiteral("Дисковий простір"));
    auto *diskLayout = new QVBoxLayout(diskBox);
    m_diskTable = new QTableWidget(0, ColCount);
    m_diskTable->setHorizontalHeaderLabels(
        {QStringLiteral("Точка монтування"), QStringLiteral("Пристрій"),
         QStringLiteral("Тип ФС"), QStringLiteral("Обсяг"), QStringLiteral("Зайнято"),
         QStringLiteral("Вільно"), QStringLiteral("Заповнення")});
    m_diskTable->setEditTriggers(QAbstractItemView::NoEditTriggers);   // лише перегляд
    m_diskTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_diskTable->setAlternatingRowColors(true);
    m_diskTable->verticalHeader()->setVisible(false);
    m_diskTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_diskTable->horizontalHeader()->setSectionResizeMode(ColFill, QHeaderView::Stretch);
    diskLayout->addWidget(m_diskTable);
    root->addWidget(diskBox, 2);

    setCentralWidget(central);

    m_updatedLabel = new QLabel;
    statusBar()->addPermanentWidget(m_updatedLabel);
}

void MainWindow::buildMenu()
{
    QMenu *fileMenu = menuBar()->addMenu(QStringLiteral("&Файл"));
    QAction *saveAction = fileMenu->addAction(QStringLiteral("Зберегти звіт…"), this,
                                              &MainWindow::saveReport);
    saveAction->setShortcut(QKeySequence::Save);   // Ctrl+S
    fileMenu->addSeparator();
    QAction *quitAction =
        fileMenu->addAction(QStringLiteral("Вийти"), this, &QWidget::close);
    quitAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Q")));

    QMenu *monitorMenu = menuBar()->addMenu(QStringLiteral("&Моніторинг"));
    m_pauseAction =
        monitorMenu->addAction(QStringLiteral("Пауза"), this, &MainWindow::togglePause);
    m_pauseAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+P")));
    QAction *refreshAction = monitorMenu->addAction(QStringLiteral("Оновити зараз"), this,
                                                    &MainWindow::refreshNow);
    refreshAction->setShortcut(QKeySequence(Qt::Key_F5));

    QMenu *helpMenu = menuBar()->addMenu(QStringLiteral("&Довідка"));
    QAction *helpAction =
        helpMenu->addAction(QStringLiteral("Довідка"), this, &MainWindow::showHelp);
    helpAction->setShortcut(QKeySequence(Qt::Key_F1));
    helpMenu->addAction(QStringLiteral("Про програму"), this, &MainWindow::showAbout);
}

void MainWindow::applyLevel(QProgressBar *bar, double percent, bool valid,
                            const QString &placeholder)
{
    // Колір лише доповнює текст: значення й рівень завжди показано словами.
    const QString color = valid ? colorFor(levelFor(percent)) : QStringLiteral("#bdbdbd");
    // Таблицю стилів змінюємо лише при зміні кольору: її застосування відносно дороге.
    if (bar->property("levelColor").toString() != color) {
        bar->setProperty("levelColor", color);
        bar->setStyleSheet(QStringLiteral("QProgressBar { border: 1px solid #9e9e9e; "
                                          "border-radius: 3px; background: #f5f5f5;"
                                          " color: #212121; }"
                                          "QProgressBar::chunk { background-color: %1; }")
                               .arg(color));
    }
    bar->setValue(valid ? qRound(percent * 10.0) : 0);
    // Шаблон тексту QProgressBar замінює лише %p, %v і %m, тому "12,5 %" виводиться як є.
    bar->setFormat(valid ? formatPercent(percent) : placeholder);
}

void MainWindow::onUpdated(const Snapshot &s)
{
    // Процесор.
    if (s.cpuValid) {
        applyLevel(m_cpuBar, s.cpuPercent);
        m_cpuLabel->setText(
            QStringLiteral("навантаження: %1").arg(levelName(levelFor(s.cpuPercent))));
        QStringList cores;
        for (int i = 0; i < s.corePercents.size(); ++i)
            cores << QStringLiteral("ядро %1: %2")
                         .arg(i + 1)
                         .arg(formatPercent(s.corePercents.at(i)));
        m_coresLabel->setText(QStringLiteral("Логічних ядер: %1 (%2)")
                                  .arg(s.corePercents.size())
                                  .arg(cores.join(QStringLiteral("; "))));
    } else {
        // Відрізняємо очікування другого знімка від помилки читання /proc/stat.
        applyLevel(m_cpuBar, 0.0, false,
                   s.cpuFailed ? QStringLiteral("немає даних")
                               : QStringLiteral("вимірювання…"));
        m_cpuLabel->setText(QString());
        m_coresLabel->setText(s.cpuFailed
                                  ? QStringLiteral("Логічних ядер: немає даних")
                                  : QStringLiteral("Логічних ядер: дані з’являться після "
                                                   "другого вимірювання"));
    }

    // Оперативна пам'ять і файл підкачки.
    if (s.memoryValid) {
        const MemoryInfo &m = s.memory;
        applyLevel(m_memoryBar, m.usedPercent());
        m_memoryLabel->setText(QStringLiteral("%1 із %2 (доступно %3)")
                                   .arg(formatBytes(m.usedBytes()),
                                        formatBytes(m.totalBytes),
                                        formatBytes(m.availableBytes)));
        if (m.swapTotalBytes > 0) {
            applyLevel(m_swapBar, m.swapUsedPercent());
            m_swapLabel->setText(
                QStringLiteral("%1 із %2")
                    .arg(formatBytes(m.swapUsedBytes()), formatBytes(m.swapTotalBytes)));
        } else {
            applyLevel(m_swapBar, 0.0, false, QStringLiteral("не налаштовано"));
            m_swapLabel->setText(QStringLiteral("файл або розділ підкачки відсутній"));
        }
    } else {
        applyLevel(m_memoryBar, 0.0, false);
        applyLevel(m_swapBar, 0.0, false);
        m_memoryLabel->setText(QStringLiteral("немає даних"));
        m_swapLabel->setText(QStringLiteral("немає даних"));
    }

    if (s.cpuValid && s.memoryValid)
        m_chart->addSample(s.cpuPercent, s.memory.usedPercent());

    updateDiskTable(s.disks);

    m_updatedLabel->setText(QStringLiteral("Останнє оновлення: %1")
                                .arg(s.time.toString(QStringLiteral("HH:mm:ss"))));
    if (!s.errors.isEmpty())
        statusBar()->showMessage(s.errors.first(), 5000);
}

void MainWindow::updateDiskTable(const QList<DiskInfo> &disks)
{
    // Рядки й смуги перевикористовуються, щоб таблиця не мерехтіла.
    m_diskTable->setRowCount(disks.size());
    for (int row = 0; row < disks.size(); ++row) {
        const DiskInfo &d = disks.at(row);
        const QStringList cells = {d.mountPoint,
                                   d.device,
                                   d.fsType,
                                   formatBytes(d.totalBytes),
                                   formatBytes(d.usedBytes),
                                   formatBytes(d.availableBytes)};
        for (int col = 0; col < cells.size(); ++col) {
            QTableWidgetItem *item = m_diskTable->item(row, col);
            if (!item) {
                item = new QTableWidgetItem;
                if (col >= ColTotal)
                    item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
                m_diskTable->setItem(row, col, item);
            }
            item->setText(cells.at(col));
        }
        auto *bar = qobject_cast<QProgressBar *>(m_diskTable->cellWidget(row, ColFill));
        if (!bar) {
            bar = makeBar();
            m_diskTable->setCellWidget(row, ColFill, bar);
        }
        applyLevel(bar, d.usedPercent());
        bar->setToolTip(QStringLiteral("Рівень заповнення: %1")
                            .arg(levelName(levelFor(d.usedPercent()))));
    }
}

void MainWindow::togglePause()
{
    const bool pause = m_monitor.isRunning();
    if (pause) {
        m_monitor.stop();
        statusBar()->showMessage(
            QStringLiteral("Моніторинг призупинено. Дані на екрані не оновлюються."));
    } else {
        m_monitor.start();
        statusBar()->showMessage(QStringLiteral("Моніторинг відновлено."), 3000);
    }
    const QString text = pause ? QStringLiteral("Продовжити") : QStringLiteral("Пауза");
    m_pauseButton->setText(text);
    m_pauseAction->setText(text);
}

void MainWindow::refreshNow()
{
    m_monitor.sampleNow();
    statusBar()->showMessage(QStringLiteral("Дані оновлено вручну."), 3000);
}

void MainWindow::onIntervalChanged(int index)
{
    const int ms = m_intervalBox->itemData(index).toInt();
    m_monitor.setIntervalMs(ms);
    statusBar()->showMessage(
        QStringLiteral("Інтервал оновлення: %1.").arg(m_intervalBox->itemText(index)),
        3000);
}

void MainWindow::saveReport()
{
    Snapshot snapshot = m_monitor.lastSnapshot();
    if (!snapshot.time.isValid())
        snapshot = m_monitor.sampleNow();

    const QString suggested = QDir::home().filePath(
        QStringLiteral("resource-report-%1.txt")
            .arg(snapshot.time.toString(QStringLiteral("yyyyMMdd-HHmmss"))));
    // Підтвердження заміни запитуємо власним українськомовним діалогом,
    // а не стандартним, текст якого залежить від наявності перекладів Qt.
    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("Зберегти звіт"), suggested,
        QStringLiteral("Текстові файли (*.txt);;Усі файли (*)"), nullptr,
        QFileDialog::DontConfirmOverwrite);
    if (path.isEmpty() || (QFileInfo::exists(path) && !confirmOverwrite(path))) {
        statusBar()->showMessage(QStringLiteral("Збереження звіту скасовано."), 3000);
        return;
    }

    // QSaveFile пише у тимчасовий файл і замінює цільовий лише після успіху.
    QSaveFile file(path);
    const QByteArray data = formatReport(snapshot).toUtf8();
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() ||
        !file.commit()) {
        showInfo(QStringLiteral("Помилка збереження"),
                 QStringLiteral("Не вдалося зберегти файл «%1».\n"
                                "Перевірте, чи є права на запис до вибраного каталогу.")
                     .arg(path),
                 true);
        return;
    }
    statusBar()->showMessage(QStringLiteral("Звіт збережено: %1").arg(path), 5000);
}

bool MainWindow::confirmOverwrite(const QString &path)
{
    QMessageBox box(QMessageBox::Warning, QStringLiteral("Підтвердження заміни"),
                    QStringLiteral("Файл «%1» уже існує.\nЗамінити його новим звітом? "
                                   "Попередній вміст буде втрачено.")
                        .arg(QFileInfo(path).fileName()),
                    QMessageBox::Yes | QMessageBox::No, this);
    box.button(QMessageBox::Yes)->setText(QStringLiteral("Замінити"));
    box.button(QMessageBox::No)->setText(QStringLiteral("Скасувати"));
    box.setDefaultButton(QMessageBox::No);   // безпечна дія за замовчуванням
    return box.exec() == QMessageBox::Yes;
}

void MainWindow::showInfo(const QString &title, const QString &text, bool isError)
{
    QMessageBox box(isError ? QMessageBox::Critical : QMessageBox::Information, title,
                    text, QMessageBox::Ok, this);
    box.button(QMessageBox::Ok)->setText(QStringLiteral("Гаразд"));
    box.exec();
}

void MainWindow::showHelp()
{
    showInfo(
        QStringLiteral("Довідка"),
        QStringLiteral(
            "Програма показує завантаження процесора, використання оперативної пам'яті, "
            "файлу підкачки та заповненість дисків.\n\n"
            "Кольори смуг: зелений — норма (до 70 %), помаранчевий — підвищене "
            "(70–90 %), червоний — критичне (від 90 %).\n\n"
            "Клавіші:\n"
            "F1 — довідка;\n"
            "F5 — оновити зараз;\n"
            "Ctrl+P — пауза або продовження;\n"
            "Ctrl+S — зберегти звіт у текстовий файл;\n"
            "Ctrl+Q — вийти з програми;\n"
            "Tab / Shift+Tab — перехід між елементами вікна."));
}

void MainWindow::showAbout()
{
    showInfo(
        QStringLiteral("Про програму"),
        QStringLiteral(
            "Монітор ресурсів ПК, версія %1\n"
            "Курсовий проєкт з дисципліни «Системне програмування».\n"
            "Мова C++17, бібліотека Qt %2.\n"
            "Джерела даних: /proc/stat, /proc/meminfo, /proc/self/mounts, statvfs().")
            .arg(QStringLiteral("1.0.0"), QString::fromLatin1(qVersion())));
}
