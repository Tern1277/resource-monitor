// main.cpp — точка входу. Режими запуску:
//   resource-monitor            — графічний інтерфейс;
//   resource-monitor --report   — одноразовий текстовий звіт у стандартний вивід.
#include "formatting.h"
#include "mainwindow.h"
#include "resourcemonitor.h"

#include <QApplication>
#include <QCoreApplication>
#include <QLibraryInfo>
#include <QLocale>
#include <QStyleFactory>
#include <QTextStream>
#include <QThread>
#include <QTranslator>

#include <cstring>

namespace {

/// Консольний режим: два знімки з інтервалом 1 с і вивід звіту.
int runReport(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    rm::ResourceMonitor monitor;   // перший знімок CPU робиться в конструкторі
    QThread::msleep(rm::ResourceMonitor::kDefaultIntervalMs);
    const rm::Snapshot snapshot = monitor.sampleNow();
    QTextStream(stdout) << rm::formatReport(snapshot);
    return (snapshot.cpuValid && snapshot.memoryValid) ? 0 : 1;
}

}   // namespace

int main(int argc, char *argv[])
{
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--report") == 0)
            return runReport(argc, argv);
    }

    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("ResourceMonitor"));
    QApplication::setApplicationDisplayName(QStringLiteral("Монітор ресурсів ПК"));
    QApplication::setApplicationVersion(QStringLiteral("1.0.0"));
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

    // Українські написи стандартних діалогів Qt (пакет qt6-translations-l10n),
    // якщо переклад установлено в системі; інакше програма працює без нього.
    QTranslator qtTranslator;
    if (qtTranslator.load(QLocale(QLocale::Ukrainian), QStringLiteral("qtbase"),
                          QStringLiteral("_"),
                          QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
        QApplication::installTranslator(&qtTranslator);

    MainWindow window;
    window.show();
    return app.exec();
}
