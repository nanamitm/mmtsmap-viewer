#pragma once

#include <QString>
#include <QVector>
#include <cstdint>

// Self-contained parser for the .mmtsmap sidecar produced by dantto4k's
// MmtsMapWriter and consumed by mmts-dsfilter. Three on-disk formats are
// supported:
//   - "MMTSMAP3" : binary v3 (current; tracks carry audioMode/channels)
//   - "MMTSMAP2" : binary v2 (no audioMode/channels)
//   - "MMTSMAP 1": text
// All integers in the binary formats are little-endian (native x64 layout the
// writer emits with raw std::ofstream::write).

struct MmtsTrack {
    QString type;              // "video" | "audio" | "subtitle"
    int streamIndex = -1;
    quint16 packetId = 0;
    int componentTag = -1;
    quint32 samplingRate = 0;  // audio only
    bool latm = false;         // audio only (22.2ch / LATM flag)
    quint8 audioMode = 0;      // audio only (v3 / text)
    quint16 channels = 0;      // audio only (v3 / text)

    QString shortDesc() const;  // e.g. "audio #2 pid=0x1812"
    QString audioLabel() const; // audio details incl. channel count, e.g.
                                // "A#2 pid=0x1812 tag=16 48kHz 5.1ch(mode9)"
    QString subtitleLabel() const; // e.g. "S#3 pid=0x1F40 tag=56"
    // Identity used to track an audio lane across MPT changes. Mirrors the
    // writer's TrackInfo::key (includes audioMode/channels for v3).
    QString audioKey() const;
};

struct MmtsPoint {
    qint64 timeMs = -1;        // absolute video PTS in ms
    quint64 offset = 0;        // byte offset into the source .mmts
};

struct MmtsMptChange {
    qint64 timeMs = -1;
    quint64 offset = 0;
    QVector<MmtsTrack> tracks; // active audio/subtitle tracks at this point
};

struct MmtsMapData {
    bool ok = false;
    QString error;

    QString formatName;        // human label e.g. "Binary MMTSMAP3 (v3)"
    quint32 version = 0;
    bool binary = false;

    quint64 sourceSize = 0;
    qint64 durationMs = 0;
    qint64 firstVideoPtsMs = -1;
    qint64 lastVideoPtsMs = -1;

    QVector<MmtsTrack> tracks;
    QVector<MmtsMptChange> mptChanges;
    QVector<MmtsPoint> rapPoints;
    QVector<MmtsPoint> seekPoints;

    // Convenience for the timeline view.
    quint64 maxOffset() const;
    qint64 maxTimeMs() const;

    // Time base used to turn the absolute PTS times stored in the file into a
    // 0-based program timeline: firstVideoPtsMs when present, otherwise the
    // smallest time seen. Subtract from any timeMs to get relative ms.
    qint64 baseTimeMs() const;
    // Length of the relative timeline in ms (>= 1).
    qint64 spanMs() const;
};

// One audio track and the program-time intervals over which it is active,
// derived from the MPT changes. Times are relative to baseTimeMs().
struct MmtsAudioInterval {
    qint64 startMs = 0;
    qint64 endMs = 0;
};
struct MmtsAudioLane {
    MmtsTrack track;   // representative track (carries channels/rate/etc.)
    QString label;     // audioLabel() of the track
    QVector<MmtsAudioInterval> intervals;
};

// Builds the audio lanes (one per distinct audio track) with their active
// intervals across the program. Adjacent intervals are merged so a track that
// stays active across several MPT changes shows as one continuous bar.
QVector<MmtsAudioLane> audioLanesFrom(const MmtsMapData& data);

namespace MmtsMapFile {
// Loads and parses the given path. On failure returns a struct with ok=false
// and a populated error string.
MmtsMapData load(const QString& path);
}
