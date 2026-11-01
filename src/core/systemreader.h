// systemreader.h — структури даних і функції читання стану ресурсів Linux.
// Аналізатори (parse*) працюють із текстом і не звертаються до ОС, тому їх
// можна перевіряти модульними тестами на заздалегідь підготовлених рядках.
#pragma once

#include <QList>
#include <QString>
#include <QStringList>
#include <QtGlobal>

namespace rm {

/// Лічильники часу процесора з рядка "cpu…" файлу /proc/stat (одиниця — USER_HZ).
/// Поля guest і guest_nice не враховуються: вони вже входять до user і nice.
struct CpuTimes {
    quint64 user = 0;
    quint64 nice = 0;
    quint64 system = 0;
    quint64 idle = 0;
    quint64 iowait = 0;
    quint64 irq = 0;
    quint64 softirq = 0;
    quint64 steal = 0;

    /// Час бездіяльності: власне простій та очікування введення-виведення.
    quint64 idleAll() const { return idle + iowait; }
    /// Загальний час, що минув з моменту завантаження системи.
    quint64 total() const
    {
        return user + nice + system + idle + iowait + irq + softirq + steal;
    }
};

/// Сумарні лічильники процесора та лічильники окремих логічних ядер.
struct CpuStat {
    CpuTimes total;
    QList<CpuTimes> cores;
};

/// Стан оперативної пам'яті та файлу підкачки у байтах.
struct MemoryInfo {
    quint64 totalBytes = 0;
    quint64 availableBytes = 0;
    quint64 swapTotalBytes = 0;
    quint64 swapFreeBytes = 0;

    quint64 usedBytes() const;
    quint64 swapUsedBytes() const;
    double usedPercent() const;
    double swapUsedPercent() const;
};

/// Один запис таблиці змонтованих файлових систем (/proc/self/mounts).
struct MountEntry {
    QString device;
    QString mountPoint;
    QString fsType;
};

/// Заповненість однієї файлової системи у байтах.
struct DiskInfo {
    QString device;
    QString mountPoint;
    QString fsType;
    quint64 totalBytes = 0;
    quint64 usedBytes = 0;
    quint64 availableBytes = 0;   // доступно звичайному користувачу

    double usedPercent() const;
};

/// Рівень навантаження для кольорового й текстового позначення.
enum class LoadLevel { Normal, Warning, Critical };

constexpr double kWarningThreshold = 70.0;    // від 70 % — підвищене
constexpr double kCriticalThreshold = 90.0;   // від 90 % — критичне

// ---------- Чисті функції аналізу тексту ----------
bool parseCpuLine(const QString &line, CpuTimes &out);
bool parseProcStat(const QString &text, CpuStat &out);
double cpuUsagePercent(const CpuTimes &previous, const CpuTimes &current);
bool parseMeminfo(const QString &text, MemoryInfo &out);
QString unescapeMountField(const QString &field);
QList<MountEntry> parseMounts(const QString &text);
QList<MountEntry> selectPhysicalMounts(const QList<MountEntry> &all);
LoadLevel levelFor(double percent);

// ---------- Звернення до операційної системи ----------
bool readTextFile(const QString &path, QString &text, QString *error = nullptr);
bool readCpuStat(CpuStat &out, QString *error = nullptr);
bool readMemoryInfo(MemoryInfo &out, QString *error = nullptr);
bool queryDisk(const MountEntry &mount, DiskInfo &out, QString *error = nullptr);
QList<DiskInfo> readDisks(QStringList *errors = nullptr);

}   // namespace rm
