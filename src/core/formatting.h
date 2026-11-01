// formatting.h — перетворення числових даних у зрозумілі користувачу рядки.
#pragma once

#include "systemreader.h"

#include <QString>

namespace rm {

struct Snapshot;

/// Розмір у двійкових одиницях: "512 Б", "1,5 ГіБ" тощо.
QString formatBytes(quint64 bytes);
/// Відсоток з одним знаком після коми: "12,5 %".
QString formatPercent(double percent);
/// Словесна назва рівня навантаження: "норма", "підвищене", "критичне".
QString levelName(LoadLevel level);
/// Текстовий звіт про стан ресурсів (формат описано в розділі 2 записки).
QString formatReport(const Snapshot &snapshot);

}   // namespace rm
