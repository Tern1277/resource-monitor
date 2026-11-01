# Монітор ресурсів ПК (CPU, RAM, диск)

Настільна програма для ОС на базі ядра Linux, що показує завантаження процесора
(загальне та за ядрами), використання оперативної пам'яті й файлу підкачки та
заповненість файлових систем. Мова C++17, бібліотека Qt 6 (Core, Widgets), CMake.

## Залежності (Ubuntu 24.04 LTS / Linux Mint 22)

    sudo apt install build-essential cmake qtcreator \
        qt6-base-dev qt6-translations-l10n

## Збирання в Qt Creator

1. File > Open Project (Ctrl+Shift+O) і вибрати файл CMakeLists.txt.
2. На вкладці Configure Project вибрати комплект Qt 6 і натиснути Configure Project.
3. Ctrl+B — зібрати, Ctrl+R — запустити ціль resource-monitor.
4. Тести: Tools > Tests > Run All Tests або запуск цілі test_core.

## Збирання з термінала

    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build -j
    ctest --test-dir build --output-on-failure
    ./build/resource-monitor            # графічний режим
    ./build/resource-monitor --report   # текстовий звіт у консоль

## Структура

    src/core  — збирання даних (/proc/stat, /proc/meminfo, /proc/self/mounts, statvfs)
    src/gui   — головне вікно та графік історії
    tests     — модульні тести Qt Test
