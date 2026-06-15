#include "AudioLanesView.h"

#include <QMouseEvent>
#include <QPainter>
#include <algorithm>

namespace {
constexpr int kGutter = 230; // left label column width
constexpr int kMarginT = 24;
constexpr int kMarginB = 30;
constexpr int kMarginR = 16;
constexpr int kLaneH = 34;
constexpr int kLaneGap = 6;

QString msToClock(qint64 ms)
{
    if (ms < 0)
        return QStringLiteral("-");
    const qint64 sec = ms / 1000;
    return QStringLiteral("%1:%2:%3")
        .arg(sec / 3600, 2, 10, QLatin1Char('0'))
        .arg((sec % 3600) / 60, 2, 10, QLatin1Char('0'))
        .arg(sec % 60, 2, 10, QLatin1Char('0'));
}

// A small fixed palette so lanes are visually distinct.
QColor laneColor(int i)
{
    static const QColor colors[] = {
        QColor(0x4f, 0xb0, 0xff), QColor(0x6f, 0xd6, 0x8c), QColor(0xff, 0xab, 0x40),
        QColor(0xc6, 0x8c, 0xff), QColor(0xff, 0x7a, 0x90), QColor(0x4f, 0xd6, 0xc8),
        QColor(0xe6, 0xd0, 0x5a),
    };
    return colors[i % (sizeof(colors) / sizeof(colors[0]))];
}
} // namespace

AudioLanesView::AudioLanesView(QWidget* parent) : QWidget(parent)
{
    setMouseTracking(true);
    setAutoFillBackground(true);
    setMinimumHeight(140);
}

void AudioLanesView::setData(const MmtsMapData& data)
{
    m_data = data;
    m_lanes = audioLanesFrom(data);
    m_changeMs.clear();
    if (data.ok) {
        const qint64 base = data.baseTimeMs();
        for (const auto& c : data.mptChanges)
            if (c.timeMs >= 0)
                m_changeMs.push_back(c.timeMs - base);
        std::sort(m_changeMs.begin(), m_changeMs.end());
    }
    m_hasData = data.ok;
    updateGeometry();
    update();
}

void AudioLanesView::clear()
{
    m_hasData = false;
    m_lanes.clear();
    m_changeMs.clear();
    updateGeometry();
    update();
}

QSize AudioLanesView::sizeHint() const
{
    const int lanes = std::max(1, static_cast<int>(m_lanes.size()));
    return QSize(640, kMarginT + kMarginB + lanes * (kLaneH + kLaneGap));
}

void AudioLanesView::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.fillRect(rect(), QColor(0x20, 0x22, 0x28));

    if (!m_hasData) {
        p.setPen(QColor(0x99, 0x9f, 0xaa));
        p.drawText(rect(), Qt::AlignCenter, tr("Open a .mmtsmap file to view audio-track activity."));
        return;
    }
    if (m_lanes.isEmpty()) {
        p.setPen(QColor(0x99, 0x9f, 0xaa));
        p.drawText(rect(), Qt::AlignCenter,
                   tr("No audio-track changes recorded (no MPT entries with audio)."));
        return;
    }

    const qreal plotL = kGutter;
    const qreal plotR = width() - kMarginR;
    const qreal plotW = std::max<qreal>(plotR - plotL, 1);
    const qreal span = static_cast<qreal>(std::max<qint64>(m_data.spanMs(), 1));
    auto X = [&](qint64 relMs) { return plotL + (relMs / span) * plotW; };

    QFont small = p.font();
    small.setPointSizeF(small.pointSizeF() - 1.0);

    // Time grid + axis labels along the top.
    p.setFont(small);
    for (int i = 0; i <= 6; ++i) {
        const qreal x = plotL + plotW * i / 6.0;
        p.setPen(QColor(0x33, 0x37, 0x40));
        p.drawLine(QPointF(x, kMarginT), QPointF(x, height() - kMarginB));
        p.setPen(QColor(0x9a, 0xa0, 0xac));
        p.drawText(QRectF(x - 40, height() - kMarginB + 2, 80, 16),
                   Qt::AlignHCenter | Qt::AlignTop,
                   msToClock(static_cast<qint64>(span * i / 6.0)));
    }

    // MPT change boundaries.
    p.setPen(QPen(QColor(0x66, 0x6c, 0x78), 1.0, Qt::DotLine));
    for (qint64 t : m_changeMs)
        p.drawLine(QPointF(X(t), kMarginT), QPointF(X(t), height() - kMarginB));

    // Lanes.
    for (int i = 0; i < m_lanes.size(); ++i) {
        const qreal y = kMarginT + i * (kLaneH + kLaneGap);
        const QRectF laneRect(0, y, width(), kLaneH);

        if (i % 2 == 0)
            p.fillRect(QRectF(plotL, y, plotW, kLaneH), QColor(0x26, 0x29, 0x30));

        // Label in the gutter.
        p.setPen(QColor(0xd6, 0xda, 0xe2));
        p.setFont(small);
        p.drawText(QRectF(8, y, kGutter - 14, kLaneH),
                   Qt::AlignLeft | Qt::AlignVCenter, m_lanes[i].label);

        // Active-interval bars.
        const QColor c = laneColor(i);
        for (const auto& iv : m_lanes[i].intervals) {
            const qreal x0 = X(iv.startMs);
            const qreal x1 = X(iv.endMs);
            QRectF bar(x0, y + 6, std::max<qreal>(x1 - x0, 2.0), kLaneH - 12);
            p.fillRect(bar, c);
            p.setPen(c.darker(140));
            p.drawRect(bar);
        }
        Q_UNUSED(laneRect);
    }

    // Hover guide + time readout.
    if (m_hoverX >= plotL && m_hoverX <= plotR) {
        p.setPen(QColor(0xb0, 0xb6, 0xc2));
        p.drawLine(QPointF(m_hoverX, kMarginT), QPointF(m_hoverX, height() - kMarginB));
        const qint64 t = static_cast<qint64>((m_hoverX - plotL) / plotW * span);
        p.setPen(QColor(0xe6, 0xe9, 0xef));
        p.drawText(QRectF(plotL, 2, plotW, 18), Qt::AlignRight | Qt::AlignTop, msToClock(t));
    }
}

void AudioLanesView::mouseMoveEvent(QMouseEvent* event)
{
    m_hoverX = event->position().x();
    update();
}

void AudioLanesView::leaveEvent(QEvent*)
{
    m_hoverX = -1;
    update();
}
