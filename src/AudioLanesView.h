#pragma once

#include "MmtsMapFile.h"
#include <QWidget>

// Horizontal-lane view of audio-track activity over the program timeline. One
// row per distinct audio track; coloured bars mark the intervals where that
// track is present in the MPT. Makes audio-track transitions (added / removed /
// channel-mode change) visible at a glance. MPT change boundaries are drawn as
// faint vertical lines.
class AudioLanesView : public QWidget {
    Q_OBJECT
public:
    explicit AudioLanesView(QWidget* parent = nullptr);

    void setData(const MmtsMapData& data);
    void clear();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;
    QSize sizeHint() const override;

private:
    MmtsMapData m_data;
    QVector<MmtsAudioLane> m_lanes;
    QVector<qint64> m_changeMs; // relative MPT change times
    bool m_hasData = false;
    qreal m_hoverX = -1;
};
