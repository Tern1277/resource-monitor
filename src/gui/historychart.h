// historychart.h — віджет графіка історії завантаження CPU і RAM.
#pragma once

#include <QList>
#include <QWidget>

/// Малює дві ламані лінії (процесор і пам'ять) за останні kCapacity вимірювань.
/// Реалізовано засобами QPainter, без додаткового модуля Qt Charts.
class HistoryChart : public QWidget
{
    Q_OBJECT

public:
    static constexpr int kCapacity = 60;

    explicit HistoryChart(QWidget *parent = nullptr);

    void addSample(double cpuPercent, double memoryPercent);
    void clear();
    int sampleCount() const;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QList<double> m_cpu;
    QList<double> m_memory;
};
