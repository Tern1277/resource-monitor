// historychart.cpp — відмальовування графіка історії навантаження.
#include "historychart.h"

#include <QPainter>
#include <QPainterPath>

namespace {
const QColor kCpuColor(21, 101, 192);      // синій — процесор
const QColor kMemoryColor(106, 27, 154);   // фіолетовий — пам'ять

/// Додає значення в кінець списку, видаляючи найстаріше при переповненні.
void pushLimited(QList<double> &list, double value, int capacity)
{
    list.append(qBound(0.0, value, 100.0));
    while (list.size() > capacity)
        list.removeFirst();
}
}   // namespace

HistoryChart::HistoryChart(QWidget *parent) : QWidget(parent)
{
    setMinimumHeight(160);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setToolTip(
        QStringLiteral("Історія завантаження за останні %1 вимірювань").arg(kCapacity));
}

void HistoryChart::addSample(double cpuPercent, double memoryPercent)
{
    pushLimited(m_cpu, cpuPercent, kCapacity);
    pushLimited(m_memory, memoryPercent, kCapacity);
    update();   // запит на перемальовування
}

void HistoryChart::clear()
{
    m_cpu.clear();
    m_memory.clear();
    update();
}

int HistoryChart::sampleCount() const
{
    return m_cpu.size();
}

void HistoryChart::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), palette().base());

    const QFontMetrics fm(font());
    const int left = fm.horizontalAdvance(QStringLiteral("100 %")) + 8;
    const int top = fm.height() + 6;   // місце для легенди
    const QRect plot(left, top, width() - left - 8, height() - top - 8);
    if (plot.width() < 20 || plot.height() < 20)
        return;

    // Сітка з підписами 0, 25, 50, 75, 100 %.
    p.setPen(QPen(palette().mid().color(), 1, Qt::DotLine));
    for (int percent = 0; percent <= 100; percent += 25) {
        const int y = plot.bottom() - plot.height() * percent / 100;
        p.drawLine(plot.left(), y, plot.right(), y);
        p.drawText(QRect(0, y - fm.height() / 2, left - 4, fm.height()),
                   Qt::AlignRight | Qt::AlignVCenter,
                   QStringLiteral("%1 %").arg(percent));
    }
    p.setPen(palette().text().color());
    p.drawRect(plot);

    // Ламана лінія для одного ряду значень; нові точки праворуч.
    auto drawSeries = [&](const QList<double> &values, const QColor &color) {
        if (values.size() < 2)
            return;
        const double step = static_cast<double>(plot.width()) / (kCapacity - 1);
        const double x0 = plot.right() - step * (values.size() - 1);
        QPainterPath path;
        for (int i = 0; i < values.size(); ++i) {
            const QPointF point(x0 + step * i,
                                plot.bottom() - plot.height() * values.at(i) / 100.0);
            if (i == 0)
                path.moveTo(point);
            else
                path.lineTo(point);
        }
        p.setPen(QPen(color, 2));
        p.drawPath(path);
    };
    drawSeries(m_cpu, kCpuColor);
    drawSeries(m_memory, kMemoryColor);

    // Легенда над графіком: кольоровий маркер і підпис.
    int x = plot.left();
    const auto legend = [&](const QColor &color, const QString &text) {
        p.fillRect(QRect(x, 4, 14, fm.height() - 4), color);
        x += 18;
        p.setPen(palette().text().color());
        p.drawText(QPoint(x, fm.ascent() + 2), text);
        x += fm.horizontalAdvance(text) + 16;
    };
    legend(kCpuColor, QStringLiteral("Процесор"));
    legend(kMemoryColor, QStringLiteral("Оперативна пам'ять"));

    if (m_cpu.isEmpty()) {
        p.setPen(palette().placeholderText().color());
        p.drawText(plot, Qt::AlignCenter,
                   QStringLiteral("Очікування перших вимірювань…"));
    }
}
