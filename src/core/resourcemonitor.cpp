// resourcemonitor.cpp — реалізація циклу опитування ресурсів.
#include "resourcemonitor.h"

namespace rm {

ResourceMonitor::ResourceMonitor(QObject *parent) : QObject(parent)
{
    m_timer.setInterval(kDefaultIntervalMs);
    connect(&m_timer, &QTimer::timeout, this, &ResourceMonitor::onTimeout);

    resetCpuBaseline();
}

void ResourceMonitor::resetCpuBaseline()
{
    // Початкові лічильники CPU: обчислення відсотка потребує двох знімків.
    m_hasPreviousCpu = readCpuStat(m_previousCpu);
    m_cpuClock.start();
}

void ResourceMonitor::start()
{
    // Після паузи перше значення має відображати лише новий інтервал,
    // а не середнє за весь час простою, тому оновлюємо базу.
    resetCpuBaseline();
    m_timer.start();
}

void ResourceMonitor::stop()
{
    m_timer.stop();
}

bool ResourceMonitor::isRunning() const
{
    return m_timer.isActive();
}

void ResourceMonitor::setIntervalMs(int ms)
{
    // Значення поза допустимими межами обмежуємо, а не відкидаємо.
    m_timer.setInterval(qBound(kMinIntervalMs, ms, kMaxIntervalMs));
}

int ResourceMonitor::intervalMs() const
{
    return m_timer.interval();
}

const Snapshot &ResourceMonitor::lastSnapshot() const
{
    return m_last;
}

void ResourceMonitor::onTimeout()
{
    sampleNow();
}

Snapshot ResourceMonitor::sampleNow()
{
    Snapshot snapshot;
    snapshot.time = QDateTime::currentDateTime();
    QString error;

    // 1. Процесор: різниця між поточними та попередніми лічильниками.
    CpuStat current;
    if (m_hasPreviousCpu && m_cpuClock.elapsed() < kMinCpuWindowMs) {
        // Надто короткий проміжок: приріст лічильників у тіках (зазвичай 10 мс)
        // дав би випадкове значення. Базу не змінюємо, а у знімок переносимо
        // попереднє значення, щоб індикатор і звіт не втрачали дані про CPU.
        snapshot.cpuValid = m_last.cpuValid;
        snapshot.cpuPercent = m_last.cpuPercent;
        snapshot.corePercents = m_last.corePercents;
    } else if (readCpuStat(current, &error)) {
        if (m_hasPreviousCpu) {
            snapshot.cpuValid = true;
            snapshot.cpuPercent = cpuUsagePercent(m_previousCpu.total, current.total);
            // Кількість ядер може змінитися (вимкнення ядра) — порівнюємо спільні.
            const int cores = qMin(current.cores.size(), m_previousCpu.cores.size());
            for (int i = 0; i < cores; ++i)
                snapshot.corePercents.append(
                    cpuUsagePercent(m_previousCpu.cores.at(i), current.cores.at(i)));
        }
        m_previousCpu = current;
        m_hasPreviousCpu = true;
        m_cpuClock.restart();
    } else {
        snapshot.cpuFailed = true;
        snapshot.errors.append(error);
    }

    // 2. Оперативна пам'ять і файл підкачки.
    if (readMemoryInfo(snapshot.memory, &error))
        snapshot.memoryValid = true;
    else
        snapshot.errors.append(error);

    // 3. Файлові системи на блокових пристроях.
    snapshot.disks = readDisks(&snapshot.errors);

    m_last = snapshot;
    emit updated(snapshot);
    return snapshot;
}

}   // namespace rm
