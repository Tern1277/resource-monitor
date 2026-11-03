// systemreader_linux.cpp — читання /proc/stat, /proc/meminfo, /proc/self/mounts
// та виклик statvfs() для визначення заповненості файлових систем.
#include "systemreader.h"

#include <cerrno>
#include <fcntl.h>
#include <sys/statvfs.h>
#include <unistd.h>

namespace rm {

namespace {

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
