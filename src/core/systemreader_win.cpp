// systemreader_win.cpp — звернення до ОС Windows через WinAPI:
//   процесор  — GetSystemTimes() і NtQuerySystemInformation() (по ядрах);
//   пам'ять   — GlobalMemoryStatusEx();
//   диски     — GetLogicalDriveStringsW(), GetVolumeInformationW(), GetDiskFreeSpaceExW().
// Дані перекладаються в ті самі структури, що й на Linux, тож решта програми
// (обчислення відсотків, інтерфейс, звіт) працює без змін.
#include "systemreader.h"

#include <QFile>
#include <QFileInfo>
#include <QVector>

#include <cwchar>
#include <string>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace rm {

namespace {

/// Опис помилки WinAPI українською мовою (аналог systemError() для Linux).
QString winError(const QString &action, const QString &path, DWORD code)
{
    QString reason;
    switch (code) {
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND:
    case ERROR_INVALID_NAME:
    case ERROR_INVALID_DRIVE:
        reason = QStringLiteral("файл або каталог не існує");
        break;
    case ERROR_ACCESS_DENIED:
        reason = QStringLiteral("недостатньо прав доступу");
        break;
    case ERROR_NOT_READY:
        reason = QStringLiteral("пристрій не готовий");
        break;
    default:
        reason = QStringLiteral("системна помилка (код %1)").arg(code);
        break;
    }
    return QStringLiteral("%1 «%2»: %3.").arg(action, path, reason);
}

quint64 toU64(const FILETIME &ft)
{
    return (static_cast<quint64>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
}

/// Відповідає SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION з winternl.h.
struct ProcessorPerformance {
    LARGE_INTEGER idleTime;
    LARGE_INTEGER kernelTime;   // включає idleTime
    LARGE_INTEGER userTime;
    LARGE_INTEGER dpcTime;
    LARGE_INTEGER interruptTime;
    ULONG interruptCount;
};

constexpr int kSystemProcessorPerformanceInformation = 8;
using NtQuerySystemInformationFn = LONG(WINAPI *)(int, PVOID, ULONG, PULONG);

/// Лічильники кожного логічного ядра (одиниця — 100 нс). Функцію беремо з
/// ntdll.dll під час виконання, щоб не залежати від бібліотеки імпорту.
QList<CpuTimes> readCoreTimes()
{
    QList<CpuTimes> cores;
    static const auto query = reinterpret_cast<NtQuerySystemInformationFn>(
        reinterpret_cast<void *>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"),
                                                "NtQuerySystemInformation")));
    if (!query)
        return cores;
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    QVector<ProcessorPerformance> buffer(static_cast<int>(si.dwNumberOfProcessors));
    ULONG returned = 0;
    const ULONG size = static_cast<ULONG>(buffer.size() * sizeof(ProcessorPerformance));
    if (query(kSystemProcessorPerformanceInformation, buffer.data(), size, &returned) != 0)
        return cores;
    const int count = static_cast<int>(returned / sizeof(ProcessorPerformance));
    for (int i = 0; i < count; ++i) {
        const ProcessorPerformance &p = buffer.at(i);
        CpuTimes t;
        t.idle = static_cast<quint64>(p.idleTime.QuadPart);
        const quint64 kernel = static_cast<quint64>(p.kernelTime.QuadPart);
        t.system = kernel > t.idle ? kernel - t.idle : 0;
        t.user = static_cast<quint64>(p.userTime.QuadPart);
        cores.append(t);
    }
    return cores;
}

}   // namespace

bool readTextFile(const QString &path, QString &text, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = winError(QStringLiteral("Не вдалося відкрити"), path,
                              QFileInfo::exists(path) ? ERROR_ACCESS_DENIED
                                                      : ERROR_FILE_NOT_FOUND);
        return false;
    }
    text = QString::fromUtf8(file.readAll());
    return true;
}

bool readCpuStat(CpuStat &out, QString *error)
{
    FILETIME idle, kernel, user;
    if (!GetSystemTimes(&idle, &kernel, &user)) {
        if (error)
            *error = winError(QStringLiteral("Помилка виклику"),
                              QStringLiteral("GetSystemTimes"), GetLastError());
        return false;
    }
    CpuStat stat;
    stat.total.idle = toU64(idle);
    const quint64 kernelAll = toU64(kernel);   // час ядра ОС разом із простоєм
    stat.total.system = kernelAll > stat.total.idle ? kernelAll - stat.total.idle : 0;
    stat.total.user = toU64(user);
    stat.cores = readCoreTimes();
    if (stat.cores.isEmpty())
        stat.cores.append(stat.total);
    out = stat;
    return true;
}

bool readMemoryInfo(MemoryInfo &out, QString *error)
{
    MEMORYSTATUSEX ms;
    ms.dwLength = sizeof ms;
    if (!GlobalMemoryStatusEx(&ms)) {
        if (error)
            *error = winError(QStringLiteral("Помилка виклику"),
                              QStringLiteral("GlobalMemoryStatusEx"), GetLastError());
        return false;
    }
    MemoryInfo info;
    info.totalBytes = ms.ullTotalPhys;
    info.availableBytes = ms.ullAvailPhys;
    // Ліміт виділення (commit limit) = фізична пам'ять + файл підкачки, тому
    // розмір і вільне місце файлу підкачки оцінюємо як різницю.
    info.swapTotalBytes =
        ms.ullTotalPageFile > ms.ullTotalPhys ? ms.ullTotalPageFile - ms.ullTotalPhys : 0;
    const quint64 swapFree =
        ms.ullAvailPageFile > ms.ullAvailPhys ? ms.ullAvailPageFile - ms.ullAvailPhys : 0;
    info.swapFreeBytes = qMin(swapFree, info.swapTotalBytes);
    out = info;
    return true;
}

bool queryDisk(const MountEntry &mount, DiskInfo &out, QString *error)
{
    ULARGE_INTEGER available, total, totalFree;
    const std::wstring path = mount.mountPoint.toStdWString();
    if (!GetDiskFreeSpaceExW(path.c_str(), &available, &total, &totalFree)) {
        if (error)
            *error = winError(QStringLiteral("Не вдалося отримати дані про"),
                              mount.mountPoint, GetLastError());
        return false;
    }
    DiskInfo info;
    info.device = mount.device;
    info.mountPoint = mount.mountPoint;
    info.fsType = mount.fsType;
    info.totalBytes = total.QuadPart;
    info.availableBytes = available.QuadPart;
    info.usedBytes = total.QuadPart > totalFree.QuadPart ? total.QuadPart - totalFree.QuadPart : 0;
    out = info;
    return true;
}

QList<DiskInfo> readDisks(QStringList *errors)
{
    QList<DiskInfo> result;
    wchar_t drives[512];
    const DWORD length = GetLogicalDriveStringsW(512, drives);
    if (length == 0 || length > 512) {
        if (errors)
            errors->append(winError(QStringLiteral("Помилка виклику"),
                                    QStringLiteral("GetLogicalDriveStringsW"), GetLastError()));
        return result;
    }
    // Рядок має вигляд "C:\\\0D:\\\0\0".
    for (const wchar_t *root = drives; *root; root += wcslen(root) + 1) {
        const UINT type = GetDriveTypeW(root);
        if (type != DRIVE_FIXED && type != DRIVE_REMOVABLE)
            continue;   // мережеві, оптичні та віртуальні диски пропускаємо
        wchar_t label[MAX_PATH + 1] = {};
        wchar_t fsName[MAX_PATH + 1] = {};
        if (!GetVolumeInformationW(root, label, MAX_PATH + 1, nullptr, nullptr, nullptr,
                                   fsName, MAX_PATH + 1))
            continue;   // наприклад, порожній кардрідер — пристрій не готовий
        MountEntry mount;
        mount.mountPoint = QString::fromWCharArray(root);
        const QString volumeLabel = QString::fromWCharArray(label);
        mount.device = volumeLabel.isEmpty()
                           ? (type == DRIVE_FIXED ? QStringLiteral("Локальний диск")
                                                  : QStringLiteral("Знімний диск"))
                           : volumeLabel;
        mount.fsType = QString::fromWCharArray(fsName);
        DiskInfo info;
        QString error;
        if (queryDisk(mount, info, &error)) {
            if (info.totalBytes > 0)
                result.append(info);
        } else if (errors) {
            errors->append(error);
        }
    }
    return result;
}

}   // namespace rm
