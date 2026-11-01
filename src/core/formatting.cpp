// formatting.cpp — форматування чисел і побудова текстового звіту.
#include "formatting.h"

#include "resourcemonitor.h"

#include <QLocale>
#include <QStringList>

namespace rm {

namespace {

/// Українська локаль: десяткова кома, без розділювача тисяч.
QLocale ukrainianLocale()
{
    QLocale locale(QLocale::Ukrainian, QLocale::Ukraine);
    locale.setNumberOptions(QLocale::OmitGroupSeparator);
    return locale;
}

}   // namespace

QString formatBytes(quint64 bytes)
{
    static const char *const units[] = {"Б", "КіБ", "МіБ", "ГіБ", "ТіБ", "ПіБ"};
    if (bytes < 1024)
        return QStringLiteral("%1 Б").arg(bytes);

    double value = static_cast<double>(bytes);
    int unit = 0;
    while (value >= 1024.0 && unit < 5) {
        value /= 1024.0;
        ++unit;
    }
    return QStringLiteral("%1 %2").arg(ukrainianLocale().toString(value, 'f', 1),
                                       QString::fromUtf8(units[unit]));
}

QString formatPercent(double percent)
{
    return QStringLiteral("%1 %").arg(ukrainianLocale().toString(percent, 'f', 1));
}

QString levelName(LoadLevel level)
{
    switch (level) {
    case LoadLevel::Normal:
        return QStringLiteral("норма");
    case LoadLevel::Warning:
        return QStringLiteral("підвищене");
    case LoadLevel::Critical:
        return QStringLiteral("критичне");
    }
    return QString();
}

QString formatReport(const Snapshot &s)
{
    QStringList lines;
    lines << QStringLiteral("=== Звіт монітора ресурсів ПК ===");
    lines << QStringLiteral("Дата і час: %1")
                 .arg(s.time.toString(QStringLiteral("dd.MM.yyyy HH:mm:ss")));

    if (s.cpuValid) {
        lines << QStringLiteral("Процесор: %1 (%2)")
                     .arg(formatPercent(s.cpuPercent), levelName(levelFor(s.cpuPercent)));
        for (int i = 0; i < s.corePercents.size(); ++i)
            lines << QStringLiteral("  Ядро %1: %2")
                         .arg(i + 1)
                         .arg(formatPercent(s.corePercents.at(i)));
    } else {
        lines << QStringLiteral("Процесор: немає даних");
    }

    if (s.memoryValid) {
        const MemoryInfo &m = s.memory;
        lines << QStringLiteral("Оперативна пам'ять: %1 із %2 — %3 (%4)")
                     .arg(formatBytes(m.usedBytes()), formatBytes(m.totalBytes),
                          formatPercent(m.usedPercent()),
                          levelName(levelFor(m.usedPercent())));
        if (m.swapTotalBytes == 0)
            lines << QStringLiteral("Файл підкачки: не налаштовано");
        else
            lines << QStringLiteral("Файл підкачки: %1 із %2 — %3")
                         .arg(formatBytes(m.swapUsedBytes()),
                              formatBytes(m.swapTotalBytes),
                              formatPercent(m.swapUsedPercent()));
    } else {
        lines << QStringLiteral("Оперативна пам'ять: немає даних");
    }

    lines << QStringLiteral("Диски:");
    if (s.disks.isEmpty())
        lines << QStringLiteral("  немає даних");
    for (const DiskInfo &d : s.disks) {
        lines << QStringLiteral("  %1 [%2, %3]: зайнято %4 із %5, вільно %6 — %7 (%8)")
                     .arg(d.mountPoint, d.fsType, d.device, formatBytes(d.usedBytes),
                          formatBytes(d.totalBytes), formatBytes(d.availableBytes),
                          formatPercent(d.usedPercent()),
                          levelName(levelFor(d.usedPercent())));
    }

    for (const QString &error : s.errors)
        lines << QStringLiteral("Помилка: %1").arg(error);

    return lines.join(QLatin1Char('\n')) + QLatin1Char('\n');
}

}   // namespace rm
