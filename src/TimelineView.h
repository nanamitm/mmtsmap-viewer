#pragma once

#include "MmtsMapFile.h"
#include <QWidget>

// Scatter/line view of the time -> byte-offset mapping. RAP points, seek
// points and MPT changes are drawn over a normalized time (x) / offset (y)
// plane so the monotonic seek curve and the index density are visible at a
// glance.
class TimelineView : public QWidget {
    Q_OBJECT
public:
    explicit TimelineView(QWidget* parent = nullptr);

    void setData(const MmtsMapData& data);
    void clear();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    MmtsMapData m_data;
    bool m_hasData = false;
    QPointF m_hover{-1, -1};
};
