// resourcemonitor.h — періодичне опитування ресурсів за таймером.
#pragma once

#include "systemreader.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QObject>
#include <QTimer>

namespace rm {

/// Знімок стану ресурсів на певний момент часу.
struct Snapshot {
    QDateTime time;
    bool cpuValid = false;   // false — перше опитування або помилка читання
    bool cpuFailed = false;   // true — саме помилка читання /proc/stat
    double cpuPercent = 0.0;
    QList<double> corePercents;   // завантаження окремих логічних ядер
    bool memoryValid = false;
    MemoryInfo memory;
    QList<DiskInfo> disks;
    QStringList errors;   // повідомлення про помилки цього опитування
};

/// Клас опитує /proc і statvfs() через задані інтервали та надсилає сигнал
/// updated() з новим знімком. Завантаження CPU обчислюється як різниця
/// лічильників між двома послідовними опитуваннями.
class ResourceMonitor : public QObject
{
    Q_OBJECT

public:
    static constexpr int kDefaultIntervalMs = 1000;
    static constexpr int kMinIntervalMs = 500;
    static constexpr int kMaxIntervalMs = 60000;
    /// Мінімальний проміжок між знімками CPU, за якого відсоток вважається точним.
    static constexpr int kMinCpuWindowMs = 200;

    explicit ResourceMonitor(QObject *parent = nullptr);

    void start();
    void stop();
    bool isRunning() const;
    void setIntervalMs(int ms);
    int intervalMs() const;

    /// Виконує одне опитування негайно, зберігає та повертає знімок.
    Snapshot sampleNow();
    const Snapshot &lastSnapshot() const;

signals:
    void updated(const rm::Snapshot &snapshot);

private slots:
    void onTimeout();

private:
    void resetCpuBaseline();

    QTimer m_timer;
    CpuStat m_previousCpu;
    bool m_hasPreviousCpu = false;
    QElapsedTimer m_cpuClock;   // час від попереднього знімка CPU
    Snapshot m_last;
};

}   // namespace rm
