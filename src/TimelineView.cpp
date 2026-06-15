#include "TimelineView.h"

#include <QMouseEvent>
#include <QPainter>
#include <QtMath>

namespace {
constexpr int kMarginL = 64;
constexpr int kMarginR = 16;
constexpr int kMarginT = 28;
constexpr int kMarginB = 36;

QString msToClock(qint64 ms)
{
    if (ms < 0)
        return QStringLiteral("-");
    const qint64 totalSec = ms / 1000;
    const qint64 h = totalSec / 3600;
    const qint64 m = (totalSec % 3600) / 60;
    const qint64 s = totalSec % 60;
    return QStringLiteral("%1:%2:%3")
        .arg(h, 2, 10, QLatin1Char('0'))
        .arg(m, 2, 10, QLatin1Char('0'))
        .arg(s, 2, 10, QLatin1Char('0'));
}

QString bytesHuman(double bytes)
{
    const char* units[] = { "B", "KiB", "MiB", "GiB", "TiB" };
    int u = 0;
    while (bytes >= 1024.0 && u < 4) { bytes /= 1024.0; ++u; }
    return QStringLiteral("%1 %2").arg(bytes, 0, 'f', u == 0 ? 0 : 2).arg(QLatin1String(units[u]));
}
} // namespace

TimelineView::TimelineView(QWidget* parent) : QWidget(parent)
{
    setMinimumHeight(220);
    setMouseTracking(true);
    setAutoFillBackground(true);
}

void TimelineView::setData(const MmtsMapData& data)
{
    m_data = data;
    m_hasData = data.ok;
    update();
}

void TimelineView::clear()
{
    m_hasData = false;
    update();
}

void TimelineView::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.fillRect(rect(), QColor(0x20, 0x22, 0x28));

    const QRectF plot(kMarginL, kMarginT,
                      width() - kMarginL - kMarginR,
                      height() - kMarginT - kMarginB);
    p.setPen(QColor(0x55, 0x5a, 0x66));
    p.drawRect(plot);

    if (!m_hasData) {
        p.setPen(QColor(0x99, 0x9f, 0xaa));
        p.drawText(rect(), Qt::AlignCenter, tr("Open a .mmtsmap file to view its index."));
        return;
    }

    // Times stored in the map are absolute PTS (ms); normalize to a 0-based
    // program timeline so the points span the full plot width instead of
    // collapsing against the right edge.
    const qint64 base = m_data.baseTimeMs();
    const double maxT = static_cast<double>(std::max<qint64>(m_data.spanMs(), 1));
    const double maxO = static_cast<double>(std::max<quint64>(m_data.maxOffset(), 1));

    auto X = [&](qint64 tMs) { return plot.left() + ((tMs - base) / maxT) * plot.width(); };
    auto Y = [&](double off) { return plot.bottom() - (off / maxO) * plot.height(); };

    // Grid + axis labels (time on X, offset on Y).
    p.setPen(QColor(0x35, 0x39, 0x42));
    QFont small = p.font();
    small.setPointSizeF(small.pointSizeF() - 1.5);
    p.setFont(small);
    for (int i = 0; i <= 4; ++i) {
        const double fx = plot.left() + plot.width() * i / 4.0;
        p.setPen(QColor(0x35, 0x39, 0x42));
        p.drawLine(QPointF(fx, plot.top()), QPointF(fx, plot.bottom()));
        p.setPen(QColor(0x9a, 0xa0, 0xac));
        p.drawText(QRectF(fx - 40, plot.bottom() + 4, 80, 18), Qt::AlignHCenter | Qt::AlignTop,
                   msToClock(static_cast<qint64>(maxT * i / 4.0)));

        const double fy = plot.bottom() - plot.height() * i / 4.0;
        p.setPen(QColor(0x35, 0x39, 0x42));
        p.drawLine(QPointF(plot.left(), fy), QPointF(plot.right(), fy));
        p.setPen(QColor(0x9a, 0xa0, 0xac));
        p.drawText(QRectF(0, fy - 9, kMarginL - 6, 18), Qt::AlignRight | Qt::AlignVCenter,
                   bytesHuman(maxO * i / 4.0));
    }

    // Seek points (sparser, ~5 s spacing) as a connected line.
    auto drawSeries = [&](const QVector<MmtsPoint>& pts, const QColor& color, bool line, double r) {
        p.setPen(QPen(color, 1.0));
        QPointF prev;
        bool havePrev = false;
        for (const auto& pt : pts) {
            const QPointF c(X(pt.timeMs), Y(static_cast<double>(pt.offset)));
            if (line && havePrev)
                p.drawLine(prev, c);
            prev = c;
            havePrev = true;
            p.setBrush(color);
            p.drawEllipse(c, r, r);
        }
    };

    drawSeries(m_data.seekPoints, QColor(0x4f, 0x9d, 0xff), true, 2.4);   // blue
    drawSeries(m_data.rapPoints, QColor(0x4f, 0xd0, 0x86), false, 1.3);   // green

    // MPT changes as vertical orange markers.
    p.setPen(QPen(QColor(0xff, 0xab, 0x40), 1.0, Qt::DashLine));
    for (const auto& c : m_data.mptChanges) {
        const double x = X(c.timeMs);
        p.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
    }

    // Legend.
    p.setFont(small);
    struct { QColor c; QString label; } legend[] = {
        { QColor(0x4f, 0xd0, 0x86), tr("RAP (%1)").arg(m_data.rapPoints.size()) },
        { QColor(0x4f, 0x9d, 0xff), tr("Seek (%1)").arg(m_data.seekPoints.size()) },
        { QColor(0xff, 0xab, 0x40), tr("MPT (%1)").arg(m_data.mptChanges.size()) },
    };
    double lx = plot.left() + 6;
    for (const auto& item : legend) {
        p.setBrush(item.c);
        p.setPen(Qt::NoPen);
        p.drawEllipse(QPointF(lx + 4, plot.top() - 14), 4, 4);
        p.setPen(QColor(0xc8, 0xcd, 0xd6));
        const QRectF tr(lx + 12, plot.top() - 24, 130, 20);
        p.drawText(tr, Qt::AlignLeft | Qt::AlignVCenter, item.label);
        lx += 12 + p.fontMetrics().horizontalAdvance(item.label) + 18;
    }

    // Hover readout.
    if (plot.contains(m_hover)) {
        const double tMs = (m_hover.x() - plot.left()) / plot.width() * maxT;
        const double off = (plot.bottom() - m_hover.y()) / plot.height() * maxO;
        p.setPen(QColor(0x66, 0x6c, 0x78));
        p.drawLine(QPointF(m_hover.x(), plot.top()), QPointF(m_hover.x(), plot.bottom()));
        p.setPen(QColor(0xe6, 0xe9, 0xef));
        const QString txt = QStringLiteral("%1  /  %2").arg(msToClock(static_cast<qint64>(tMs)),
                                                            bytesHuman(off));
        p.drawText(QRectF(plot.left() + 4, plot.top() + 2, plot.width() - 8, 18),
                   Qt::AlignRight | Qt::AlignTop, txt);
    }
}

void TimelineView::mouseMoveEvent(QMouseEvent* event)
{
    m_hover = event->position();
    update();
}

void TimelineView::leaveEvent(QEvent*)
{
    m_hover = QPointF(-1, -1);
    update();
}
