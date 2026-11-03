// systemreader.cpp — спільна частина: розбір тексту /proc, обчислення відсотків
// і рівнів навантаження. Не звертається до ОС, тому однакова для всіх платформ.
// Звернення до ОС: systemreader_linux.cpp (Linux) і systemreader_win.cpp (Windows).
#include "systemreader.h"

#include <QSet>

namespace rm {

namespace {

/// Обчислює частку part від whole у відсотках з обмеженням [0; 100].
double percentOf(quint64 part, quint64 whole)
{
    if (whole == 0)
        return 0.0;
    const double value = 100.0 * static_cast<double>(part) / static_cast<double>(whole);
    return qBound(0.0, value, 100.0);
}

/// Розбиває рядок на поля за довільною кількістю пробілів і табуляцій.
QStringList splitFields(const QString &line)
{
    return line.simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts);
}


}   // namespace

// ---------------- MemoryInfo / DiskInfo ----------------

quint64 MemoryInfo::usedBytes() const
{
    return totalBytes > availableBytes ? totalBytes - availableBytes : 0;
}

quint64 MemoryInfo::swapUsedBytes() const
{
    return swapTotalBytes > swapFreeBytes ? swapTotalBytes - swapFreeBytes : 0;
}

double MemoryInfo::usedPercent() const
{
    return percentOf(usedBytes(), totalBytes);
}

double MemoryInfo::swapUsedPercent() const
{
    return percentOf(swapUsedBytes(), swapTotalBytes);
}

double DiskInfo::usedPercent() const
{
    // Так само, як утиліта df: частка зайнятого від суми зайнятого
    // та доступного користувачу (зарезервовані для root блоки не враховуються).
    return percentOf(usedBytes, usedBytes + availableBytes);
}

// ---------------- Процесор ----------------

bool parseCpuLine(const QString &line, CpuTimes &out)
{
    const QStringList fields = splitFields(line);
    // Мінімально потрібні: мітка cpu/cpuN та чотири лічильники user nice system idle.
    if (fields.size() < 5 || !fields.first().startsWith(QLatin1String("cpu")))
        return false;

    quint64 values[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    const int count = qMin(8, static_cast<int>(fields.size()) - 1);
    for (int i = 0; i < count; ++i) {
        bool ok = false;
        values[i] = fields.at(i + 1).toULongLong(&ok);
        if (!ok)
            return false;   // нечислове поле — рядок пошкоджений
    }
    out.user = values[0];
    out.nice = values[1];
    out.system = values[2];
    out.idle = values[3];
    out.iowait = values[4];   // поля 5–8 відсутні в дуже старих ядрах
    out.irq = values[5];
    out.softirq = values[6];
    out.steal = values[7];
    return true;
}

bool parseProcStat(const QString &text, CpuStat &out)
{
    CpuStat result;
    bool hasTotal = false;
    const QStringList lines = text.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        if (!line.startsWith(QLatin1String("cpu")))
            continue;
        CpuTimes times;
        if (!parseCpuLine(line, times))
            return false;
        if (line.startsWith(QLatin1String("cpu "))) {   // сумарний рядок
            result.total = times;
            hasTotal = true;
        } else {   // рядки cpu0, cpu1, …
            result.cores.append(times);
        }
    }
    if (!hasTotal)
        return false;
    out = result;
    return true;
}

double cpuUsagePercent(const CpuTimes &previous, const CpuTimes &current)
{
    const quint64 prevTotal = previous.total();
    const quint64 currTotal = current.total();
    if (currTotal <= prevTotal)
        return 0.0;   // час не минув або лічильники скинуто — даних для оцінки немає

    const quint64 totalDelta = currTotal - prevTotal;
    // iowait на деяких ядрах може зменшуватися, тому від'ємний приріст вважаємо нулем.
    quint64 idleDelta = current.idleAll() >= previous.idleAll()
                            ? current.idleAll() - previous.idleAll()
                            : 0;
    idleDelta = qMin(idleDelta, totalDelta);
    return percentOf(totalDelta - idleDelta, totalDelta);
}

// ---------------- Оперативна пам'ять ----------------

bool parseMeminfo(const QString &text, MemoryInfo &out)
{
    // Значення у /proc/meminfo подано в кібібайтах із суфіксом "kB".
    QHash<QString, quint64> values;
    const QStringList lines = text.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        const int colon = line.indexOf(QLatin1Char(':'));
        if (colon <= 0)
            continue;
        const QString key = line.left(colon).trimmed();
        const QStringList rest = splitFields(line.mid(colon + 1));
        if (rest.isEmpty())
            continue;
        bool ok = false;
        quint64 value = rest.first().toULongLong(&ok);
        if (!ok)
            continue;   // невідомий формат рядка пропускаємо
        if (rest.size() > 1 && rest.at(1) == QLatin1String("kB"))
            value *= 1024;
        values.insert(key, value);
    }

    if (!values.contains(QStringLiteral("MemTotal")) ||
        values.value(QStringLiteral("MemTotal")) == 0)
        return false;

    MemoryInfo info;
    info.totalBytes = values.value(QStringLiteral("MemTotal"));
    if (values.contains(QStringLiteral("MemAvailable"))) {
        info.availableBytes = values.value(QStringLiteral("MemAvailable"));
    } else {
        // Ядра до 3.14 не мають MemAvailable — наближена оцінка.
        info.availableBytes = values.value(QStringLiteral("MemFree")) +
                              values.value(QStringLiteral("Buffers")) +
                              values.value(QStringLiteral("Cached"));
    }
    info.availableBytes = qMin(info.availableBytes, info.totalBytes);
    info.swapTotalBytes = values.value(QStringLiteral("SwapTotal"));
    info.swapFreeBytes =
        qMin(values.value(QStringLiteral("SwapFree")), info.swapTotalBytes);
    out = info;
    return true;
}

// ---------------- Файлові системи ----------------

QString unescapeMountField(const QString &field)
{
    // Ядро кодує пробіл, табуляцію, новий рядок і "\" як \040, \011, \012, \134.
    QString result;
    result.reserve(field.size());
    for (int i = 0; i < field.size(); ++i) {
        if (field.at(i) == QLatin1Char('\\') && i + 3 < field.size()) {
            const QString digits = field.mid(i + 1, 3);
            bool ok = false;
            const int code = digits.toInt(&ok, 8);
            if (ok) {
                result.append(QChar(code));
                i += 3;
                continue;
            }
        }
        result.append(field.at(i));
    }
    return result;
}

QList<MountEntry> parseMounts(const QString &text)
{
    QList<MountEntry> result;
    const QStringList lines = text.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        const QStringList fields = splitFields(line);
        if (fields.size() < 3)
            continue;   // некоректний рядок не зупиняє аналіз інших
        MountEntry entry;
        entry.device = unescapeMountField(fields.at(0));
        entry.mountPoint = unescapeMountField(fields.at(1));
        entry.fsType = fields.at(2);
        result.append(entry);
    }
    return result;
}

QList<MountEntry> selectPhysicalMounts(const QList<MountEntry> &all)
{
    // Відбираємо файлові системи на блокових пристроях (/dev/…). Образи snap
    // (squashfs) завжди заповнені на 100 % і лише заважали б користувачу.
    // Повторне монтування того самого пристрою (bind mount) показуємо один раз.
    QList<MountEntry> result;
    QSet<QString> seenDevices;
    for (const MountEntry &entry : all) {
        if (!entry.device.startsWith(QLatin1String("/dev/")))
            continue;
        if (entry.fsType == QLatin1String("squashfs"))
            continue;
        if (seenDevices.contains(entry.device))
            continue;
        seenDevices.insert(entry.device);
        result.append(entry);
    }
    return result;
}

LoadLevel levelFor(double percent)
{
    if (percent >= kCriticalThreshold)
        return LoadLevel::Critical;
    if (percent >= kWarningThreshold)
        return LoadLevel::Warning;
    return LoadLevel::Normal;
}

}   // namespace rm
