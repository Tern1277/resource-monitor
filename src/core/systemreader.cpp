// systemreader.cpp — читання /proc/stat, /proc/meminfo, /proc/self/mounts
// та виклик statvfs() для визначення заповненості файлових систем.
#include "systemreader.h"

#include <QSet>

#include <cerrno>
#include <fcntl.h>
#include <sys/statvfs.h>
#include <unistd.h>

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

/// Опис системної помилки errno українською мовою. Функція strerror() не
/// використовується, бо в локалі C вона повертає англомовний текст.
QString systemError(const QString &action, const QString &path, int code)
{
    QString reason;
    switch (code) {
    case ENOENT:
        reason = QStringLiteral("файл або каталог не існує");
        break;
    case EACCES:
    case EPERM:
        reason = QStringLiteral("недостатньо прав доступу");
        break;
    case EIO:
        reason = QStringLiteral("помилка введення-виведення");
        break;
    case ENOTDIR:
        reason = QStringLiteral("частина шляху не є каталогом");
        break;
    default:
        reason = QStringLiteral("системна помилка (код %1)").arg(code);
        break;
    }
    return QStringLiteral("%1 «%2»: %3.").arg(action, path, reason);
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

// ---------------- Звернення до ОС ----------------

bool readTextFile(const QString &path, QString &text, QString *error)
{
    // Файли /proc мають нульовий розмір у метаданих, тому читаємо їх
    // системним викликом read() блоками до кінця файлу.
    const QByteArray nativePath = path.toLocal8Bit();
    const int fd = ::open(nativePath.constData(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        if (error)
            *error = systemError(QStringLiteral("Не вдалося відкрити"), path, errno);
        return false;
    }

    QByteArray data;
    char buffer[4096];
    for (;;) {
        const ssize_t n = ::read(fd, buffer, sizeof buffer);
        if (n > 0) {
            data.append(buffer, static_cast<int>(n));
        } else if (n == 0) {
            break;   // кінець файлу
        } else if (errno == EINTR) {
            continue;   // виклик перервано сигналом — повторюємо
        } else {
            const int code = errno;
            ::close(fd);
            if (error)
                *error = systemError(QStringLiteral("Помилка читання"), path, code);
            return false;
        }
    }
    ::close(fd);
    text = QString::fromUtf8(data);
    return true;
}

bool readCpuStat(CpuStat &out, QString *error)
{
    QString text;
    if (!readTextFile(QStringLiteral("/proc/stat"), text, error))
        return false;
    if (!parseProcStat(text, out)) {
        if (error)
            *error = QStringLiteral("Невідомий формат файлу /proc/stat.");
        return false;
    }
    return true;
}

bool readMemoryInfo(MemoryInfo &out, QString *error)
{
    QString text;
    if (!readTextFile(QStringLiteral("/proc/meminfo"), text, error))
        return false;
    if (!parseMeminfo(text, out)) {
        if (error)
            *error = QStringLiteral("Невідомий формат файлу /proc/meminfo.");
        return false;
    }
    return true;
}

bool queryDisk(const MountEntry &mount, DiskInfo &out, QString *error)
{
    struct statvfs st;
    const QByteArray nativePath = mount.mountPoint.toLocal8Bit();
    if (::statvfs(nativePath.constData(), &st) != 0) {
        if (error)
            *error = systemError(QStringLiteral("Не вдалося отримати дані про"),
                                 mount.mountPoint, errno);
        return false;
    }
    const quint64 blockSize = st.f_frsize ? st.f_frsize : st.f_bsize;
    DiskInfo info;
    info.device = mount.device;
    info.mountPoint = mount.mountPoint;
    info.fsType = mount.fsType;
    info.totalBytes = static_cast<quint64>(st.f_blocks) * blockSize;
    const quint64 freeBytes = static_cast<quint64>(st.f_bfree) * blockSize;
    info.availableBytes = static_cast<quint64>(st.f_bavail) * blockSize;
    info.usedBytes = info.totalBytes > freeBytes ? info.totalBytes - freeBytes : 0;
    out = info;
    return true;
}

QList<DiskInfo> readDisks(QStringList *errors)
{
    QList<DiskInfo> result;
    QString text;
    QString error;
    if (!readTextFile(QStringLiteral("/proc/self/mounts"), text, &error)) {
        if (errors)
            errors->append(error);
        return result;
    }
    const QList<MountEntry> mounts = selectPhysicalMounts(parseMounts(text));
    for (const MountEntry &mount : mounts) {
        DiskInfo info;
        if (queryDisk(mount, info, &error)) {
            if (info.totalBytes > 0)   // порожні службові ФС не показуємо
                result.append(info);
        } else if (errors) {
            errors->append(error);
        }
    }
    return result;
}

}   // namespace rm
