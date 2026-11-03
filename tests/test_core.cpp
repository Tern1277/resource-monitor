// test_core.cpp — модульні та інтеграційні тести ядра (Qt Test).
// Запуск: ./test_core -v1  або  ctest --output-on-failure
#include "formatting.h"
#include "resourcemonitor.h"
#include "systemreader.h"

#include <QtTest>

using namespace rm;

class CoreTests : public QObject
{
    Q_OBJECT

private slots:
    // --- Процесор ---
    void parseCpuLineReadsAllCounters()
    {
        CpuTimes t;
        QVERIFY(parseCpuLine(QStringLiteral("cpu  10 2 30 400 5 6 7 8 9 0"), t));
        QCOMPARE(t.user, quint64(10));
        QCOMPARE(t.nice, quint64(2));
        QCOMPARE(t.system, quint64(30));
        QCOMPARE(t.idle, quint64(400));
        QCOMPARE(t.iowait, quint64(5));
        QCOMPARE(t.steal, quint64(8));
        QCOMPARE(t.total(), quint64(468));   // guest (9) не додається
        QCOMPARE(t.idleAll(), quint64(405));
    }

    void parseCpuLineRejectsInvalid()
    {
        CpuTimes t;
        QVERIFY(!parseCpuLine(QStringLiteral("cpu 1 2 x 4"), t));   // нечислове поле
        QVERIFY(!parseCpuLine(QStringLiteral("cpu 1 2 3"), t));   // замало полів
        QVERIFY(!parseCpuLine(QStringLiteral("intr 1 2 3 4"), t));   // інший рядок
    }

    void parseProcStatFindsTotalAndCores()
    {
        const QString text =
            QStringLiteral("cpu  100 0 50 800 50 0 0 0 0 0\n"
                           "cpu0 50 0 25 400 25 0 0 0 0 0\n"
                           "cpu1 50 0 25 400 25 0 0 0 0 0\n"
                           "intr 12345 0 0\nctxt 999\nbtime 1700000000\n");
        CpuStat stat;
        QVERIFY(parseProcStat(text, stat));
        QCOMPARE(stat.cores.size(), 2);
        QCOMPARE(stat.total.total(), quint64(1000));
        QVERIFY(!parseProcStat(QStringLiteral("intr 1\nctxt 2\n"), stat));
    }

    void cpuUsageKnownValue()
    {
        CpuTimes prev;
        prev.user = 100;
        prev.system = 50;
        prev.idle = 800;
        prev.iowait = 50;   // 1000
        CpuTimes curr;
        curr.user = 150;
        curr.system = 75;
        curr.idle = 1000;
        curr.iowait = 75;   // 1300
        // Приріст: усього 300, простій 225, робота 75 → 25 %.
        QCOMPARE(cpuUsagePercent(prev, curr), 25.0);
    }

    void cpuUsageWithoutElapsedTimeIsZero()
    {
        CpuTimes t;
        t.user = 10;
        t.idle = 90;
        QCOMPARE(cpuUsagePercent(t, t), 0.0);   // ділення на нуль не виникає
        CpuTimes older = t;
        older.idle = 200;   // лічильники "зменшилися"
        QCOMPARE(cpuUsagePercent(older, t), 0.0);
    }

    void cpuUsageHandlesIowaitDecrease()
    {
        CpuTimes prev;
        prev.user = 100;
        prev.idle = 100;
        prev.iowait = 50;
        CpuTimes curr;
        curr.user = 200;
        curr.idle = 150;
        curr.iowait = 40;   // iowait зменшився на 10
        const double usage = cpuUsagePercent(prev, curr);
        QVERIFY(usage >= 0.0 && usage <= 100.0);
        QCOMPARE(usage, 100.0 * 100 / 140);   // приріст простою 40 із 140
    }

    // --- Пам'ять ---
    void parseMeminfoUsesMemAvailable()
    {
        const QString text =
            QStringLiteral("MemTotal:        8000000 kB\nMemFree:          500000 kB\n"
                           "MemAvailable:    6000000 kB\nBuffers:          100000 kB\n"
                           "Cached:          2000000 kB\nSwapTotal:       2000000 kB\n"
                           "SwapFree:        1500000 kB\nHugePages_Total:       0\n");
        MemoryInfo m;
        QVERIFY(parseMeminfo(text, m));
        QCOMPARE(m.totalBytes, quint64(8000000) * 1024);
        QCOMPARE(m.availableBytes, quint64(6000000) * 1024);
        QCOMPARE(m.usedPercent(), 25.0);
        QCOMPARE(m.swapUsedBytes(), quint64(500000) * 1024);
        QCOMPARE(m.swapUsedPercent(), 25.0);
    }

    void parseMeminfoFallbackWithoutMemAvailable()
    {
        const QString text = QStringLiteral(
            "MemTotal: 1000 kB\nMemFree: 200 kB\nBuffers: 100 kB\nCached: 200 kB\n");
        MemoryInfo m;
        QVERIFY(parseMeminfo(text, m));
        QCOMPARE(m.availableBytes, quint64(500) * 1024);
        QCOMPARE(m.swapTotalBytes, quint64(0));
        QCOMPARE(m.swapUsedPercent(), 0.0);
    }

    void parseMeminfoRejectsMissingTotal()
    {
        MemoryInfo m;
        QVERIFY(!parseMeminfo(QStringLiteral("MemFree: 200 kB\n"), m));
        QVERIFY(!parseMeminfo(QString(), m));
    }

    // --- Диски ---
    void parseAndFilterMounts()
    {
        const QString text =
            QStringLiteral("proc /proc proc rw 0 0\n"
                           "/dev/sda2 / ext4 rw,relatime 0 0\n"
                           "tmpfs /run tmpfs rw 0 0\n"
                           "/dev/sda1 /boot/efi vfat rw 0 0\n"
                           "/dev/loop3 /snap/core/1 squashfs ro 0 0\n"
                           "/dev/sdb1 /media/user/My\\040Disk ext4 rw 0 0\n"
                           "/dev/sda2 /var/lib/docker ext4 rw 0 0\n"
                           "broken-line\n");
        const QList<MountEntry> all = parseMounts(text);
        QCOMPARE(all.size(), 7);   // некоректний рядок пропущено
        const QList<MountEntry> disks = selectPhysicalMounts(all);
        QCOMPARE(disks.size(), 3);   // /, /boot/efi, /media/user/My Disk
        QCOMPARE(disks.at(0).mountPoint, QStringLiteral("/"));
        QCOMPARE(disks.at(2).mountPoint, QStringLiteral("/media/user/My Disk"));
        QCOMPARE(unescapeMountField(QStringLiteral("a\\134b\\011c")),
                 QStringLiteral("a\\b\tc"));
    }

    void queryDiskForRoot()
    {
        MountEntry root;
        root.device = QStringLiteral("/dev/test");
#ifdef Q_OS_WIN
        root.mountPoint = QStringLiteral("C:\\");
#else
        root.mountPoint = QStringLiteral("/");
#endif
        root.fsType = QStringLiteral("test");
        DiskInfo d;
        QString error;
        QVERIFY2(queryDisk(root, d, &error), qPrintable(error));
        QVERIFY(d.totalBytes > 0);
        QVERIFY(d.usedBytes <= d.totalBytes);
        QVERIFY(d.availableBytes <= d.totalBytes);
        QVERIFY(d.usedPercent() >= 0.0 && d.usedPercent() <= 100.0);
    }

    void queryDiskReportsMissingPath()
    {
        MountEntry missing;
#ifdef Q_OS_WIN
        missing.mountPoint = QStringLiteral("C:\\шлях\\якого\\немає\\");
#else
        missing.mountPoint = QStringLiteral("/шлях/якого/немає");
#endif
        DiskInfo d;
        QString error;
        QVERIFY(!queryDisk(missing, d, &error));
        QVERIFY(error.contains(QStringLiteral("не існує")));
    }

    // --- Форматування та рівні ---
    void formatBytesUsesBinaryUnits()
    {
        QCOMPARE(formatBytes(0), QStringLiteral("0 Б"));
        QCOMPARE(formatBytes(512), QStringLiteral("512 Б"));
        QCOMPARE(formatBytes(1536), QStringLiteral("1,5 КіБ"));
        QCOMPARE(formatBytes(quint64(1) << 30), QStringLiteral("1,0 ГіБ"));
        QCOMPARE(formatPercent(12.345), QStringLiteral("12,3 %"));
    }

    void levelThresholds()
    {
        QCOMPARE(levelFor(0.0), LoadLevel::Normal);
        QCOMPARE(levelFor(69.9), LoadLevel::Normal);
        QCOMPARE(levelFor(70.0), LoadLevel::Warning);
        QCOMPARE(levelFor(89.9), LoadLevel::Warning);
        QCOMPARE(levelFor(90.0), LoadLevel::Critical);
        QCOMPARE(levelName(LoadLevel::Critical), QStringLiteral("критичне"));
    }

    // --- Інтеграція з реальною системою ---
    void readsLiveProcFiles()
    {
        CpuStat cpu;
        MemoryInfo memory;
        QString error;
        QVERIFY2(readCpuStat(cpu, &error), qPrintable(error));
        QVERIFY(cpu.cores.size() >= 1);
        QVERIFY2(readMemoryInfo(memory, &error), qPrintable(error));
        QVERIFY(memory.totalBytes > 0);
        QVERIFY(memory.availableBytes <= memory.totalBytes);
        QString text;
#ifdef Q_OS_WIN
        QVERIFY(!readTextFile(QStringLiteral("C:\\немає-такого-файлу.txt"), text, &error));
#else
        QVERIFY(!readTextFile(QStringLiteral("/proc/немає-такого-файлу"), text, &error));
#endif
    }

    void monitorProducesValidSnapshot()
    {
        ResourceMonitor monitor;
        monitor.setIntervalMs(10);
        QCOMPARE(monitor.intervalMs(), ResourceMonitor::kMinIntervalMs);
        monitor.setIntervalMs(1000000);
        QCOMPARE(monitor.intervalMs(), ResourceMonitor::kMaxIntervalMs);

        QSignalSpy spy(&monitor, &ResourceMonitor::updated);
        QTest::qWait(ResourceMonitor::kMinCpuWindowMs + 100);
        const Snapshot s = monitor.sampleNow();
        QCOMPARE(spy.count(), 1);
        QVERIFY(s.cpuValid);
        QVERIFY(s.cpuPercent >= 0.0 && s.cpuPercent <= 100.0);
        QVERIFY(s.memoryValid);
        QVERIFY(!s.disks.isEmpty());

        // Повторний знімок одразу ж: проміжок замалий, тому CPU не
        // перераховується, а у знімок переноситься попереднє значення.
        const Snapshot quick = monitor.sampleNow();
        QVERIFY(quick.cpuValid);
        QCOMPARE(quick.cpuPercent, s.cpuPercent);
        QCOMPARE(quick.corePercents, s.corePercents);

        const QString report = formatReport(s);
        QVERIFY(report.contains(QStringLiteral("Процесор: ")));
        QVERIFY(report.contains(QStringLiteral("Оперативна пам'ять: ")));
        QVERIFY(report.contains(QStringLiteral("Диски:")));
    }
};

QTEST_GUILESS_MAIN(CoreTests)
#include "test_core.moc"
