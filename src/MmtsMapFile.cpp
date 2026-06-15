#include "MmtsMapFile.h"

#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QTextStream>
#include <QtEndian>
#include <algorithm>
#include <limits>

namespace {

constexpr qint64 kMaxMapSize = 256LL * 1024 * 1024; // generous viewer cap

QString trackTypeName(quint8 code)
{
    switch (code) {
    case 1: return QStringLiteral("video");
    case 2: return QStringLiteral("audio");
    case 3: return QStringLiteral("subtitle");
    default: return QStringLiteral("unknown");
    }
}

// Little-endian sequential reader over an in-memory buffer.
class Cursor {
public:
    Cursor(const char* data, qint64 size) : m_data(data), m_size(size) {}

    bool atEnd() const { return m_pos >= m_size; }
    qint64 pos() const { return m_pos; }
    void seek(qint64 p) { m_pos = p; }

    template <typename T>
    bool read(T& out)
    {
        if (m_pos + static_cast<qint64>(sizeof(T)) > m_size)
            return false;
        T v;
        std::memcpy(&v, m_data + m_pos, sizeof(T));
        out = qFromLittleEndian(v);
        m_pos += sizeof(T);
        return true;
    }

    bool readBytes(char* dst, qint64 n)
    {
        if (m_pos + n > m_size)
            return false;
        std::memcpy(dst, m_data + m_pos, n);
        m_pos += n;
        return true;
    }

private:
    const char* m_data;
    qint64 m_size;
    qint64 m_pos = 0;
};

MmtsMapData parseBinary(Cursor& cur, bool v3, MmtsMapData out)
{
    out.binary = true;
    out.version = v3 ? 3 : 2;
    out.formatName = v3 ? QStringLiteral("Binary MMTSMAP3 (v3)")
                        : QStringLiteral("Binary MMTSMAP2 (v2)");

    quint32 version = 0, flags = 0;
    quint64 sourceSize = 0;
    qint64 durationMs = 0, firstPts = -1, lastPts = -1;
    quint32 trackCount = 0, mptCount = 0, rapCount = 0, seekCount = 0;

    if (!cur.read(version) || !cur.read(flags) || !cur.read(sourceSize) ||
        !cur.read(durationMs) || !cur.read(firstPts) || !cur.read(lastPts) ||
        !cur.read(trackCount) || !cur.read(mptCount) || !cur.read(rapCount) ||
        !cur.read(seekCount)) {
        out.error = QStringLiteral("Truncated binary header.");
        return out;
    }

    out.sourceSize = sourceSize;
    out.durationMs = durationMs;
    out.firstVideoPtsMs = firstPts;
    out.lastVideoPtsMs = lastPts;

    out.tracks.reserve(static_cast<int>(trackCount));
    for (quint32 i = 0; i < trackCount; ++i) {
        quint8 type = 0, trackFlags = 0, audioMode = 0, channels = 0;
        qint32 streamIndex = -1;
        quint16 packetId = 0;
        qint16 componentTag = -1;
        quint32 samplingRate = 0, reserved = 0;

        if (!cur.read(type) || !cur.read(trackFlags)) {
            out.error = QStringLiteral("Truncated track table.");
            return out;
        }
        if (v3) {
            if (!cur.read(audioMode) || !cur.read(channels)) {
                out.error = QStringLiteral("Truncated track table.");
                return out;
            }
        } else {
            quint16 pad = 0;
            if (!cur.read(pad)) {
                out.error = QStringLiteral("Truncated track table.");
                return out;
            }
        }
        if (!cur.read(streamIndex) || !cur.read(packetId) ||
            !cur.read(componentTag) || !cur.read(samplingRate) ||
            !cur.read(reserved)) {
            out.error = QStringLiteral("Truncated track table.");
            return out;
        }

        MmtsTrack t;
        t.type = trackTypeName(type);
        t.streamIndex = streamIndex;
        t.packetId = packetId;
        t.componentTag = componentTag;
        t.samplingRate = samplingRate;
        t.latm = (trackFlags & 1) != 0;
        t.audioMode = audioMode;
        t.channels = channels;
        out.tracks.push_back(t);
    }

    out.mptChanges.reserve(static_cast<int>(mptCount));
    for (quint32 i = 0; i < mptCount; ++i) {
        qint64 timeMs = -1;
        quint64 offset = 0;
        quint32 mptTrackCount = 0;
        if (!cur.read(timeMs) || !cur.read(offset) || !cur.read(mptTrackCount)) {
            out.error = QStringLiteral("Truncated MPT table.");
            return out;
        }
        MmtsMptChange change;
        change.timeMs = timeMs;
        change.offset = offset;
        for (quint32 j = 0; j < mptTrackCount; ++j) {
            quint32 idx = 0;
            if (!cur.read(idx)) {
                out.error = QStringLiteral("Truncated MPT track references.");
                return out;
            }
            if (idx < static_cast<quint32>(out.tracks.size()))
                change.tracks.push_back(out.tracks[static_cast<int>(idx)]);
        }
        out.mptChanges.push_back(change);
    }

    auto readPoints = [&cur](QVector<MmtsPoint>& dst, quint32 count, QString& err) {
        dst.reserve(static_cast<int>(count));
        for (quint32 i = 0; i < count; ++i) {
            qint64 timeMs = -1;
            quint64 offset = 0;
            if (!cur.read(timeMs) || !cur.read(offset)) {
                err = QStringLiteral("Truncated point table.");
                return false;
            }
            dst.push_back(MmtsPoint{ timeMs, offset });
        }
        return true;
    };
    if (!readPoints(out.rapPoints, rapCount, out.error))
        return out;
    if (!readPoints(out.seekPoints, seekCount, out.error))
        return out;

    out.ok = true;
    return out;
}

// --- text helpers ---------------------------------------------------------

qint64 parseInt(const QString& s, qint64 def)
{
    QString v = s.trimmed();
    bool ok = false;
    qint64 r;
    if (v.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive))
        r = v.mid(2).toLongLong(&ok, 16);
    else
        r = v.toLongLong(&ok, 10);
    return ok ? r : def;
}

// Parse "key=value key=value" into a lookup.
QMap<QString, QString> parseKeyValues(const QString& s)
{
    QMap<QString, QString> map;
    const QStringList parts = s.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    for (const QString& part : parts) {
        const int eq = part.indexOf(QLatin1Char('='));
        if (eq > 0)
            map.insert(part.left(eq), part.mid(eq + 1));
    }
    return map;
}

// audio spec: idx:0xPID:tag[:rate:latm:mode:channels]  subtitle: idx:0xPID:tag
QVector<MmtsTrack> parseMptTrackList(const QString& value, const QString& type)
{
    QVector<MmtsTrack> tracks;
    if (value.isEmpty() || value == QLatin1String("-"))
        return tracks;
    const QStringList specs = value.split(QLatin1Char(','), Qt::SkipEmptyParts);
    for (const QString& spec : specs) {
        const QStringList f = spec.split(QLatin1Char(':'));
        const bool isAudio = (type == QLatin1String("audio"));
        if ((isAudio && f.size() != 5 && f.size() != 7) || (!isAudio && f.size() != 3))
            continue;
        MmtsTrack t;
        t.type = type;
        t.streamIndex = static_cast<int>(parseInt(f[0], -1));
        t.packetId = static_cast<quint16>(parseInt(f[1], 0));
        t.componentTag = static_cast<int>(parseInt(f[2], -1));
        if (isAudio) {
            t.samplingRate = static_cast<quint32>(parseInt(f[3], 0));
            t.latm = parseInt(f[4], 0) != 0;
            if (f.size() >= 7) {
                t.audioMode = static_cast<quint8>(parseInt(f[5], 0));
                t.channels = static_cast<quint16>(parseInt(f[6], 0));
            }
        }
        tracks.push_back(t);
    }
    return tracks;
}

MmtsMapData parseText(QFile& file, MmtsMapData out)
{
    out.binary = false;
    out.version = 1;
    out.formatName = QStringLiteral("Text MMTSMAP 1");

    file.seek(0);
    QTextStream in(&file);
    QString magic = in.readLine();
    if (magic != QLatin1String("MMTSMAP 1")) {
        out.error = QStringLiteral("Bad magic (expected \"MMTSMAP 1\").");
        return out;
    }

    QString line;
    while (!(line = in.readLine()).isNull()) {
        if (line.isEmpty())
            continue;

        if (line.startsWith(QLatin1String("track "))) {
            const auto kv = parseKeyValues(line.mid(6));
            MmtsTrack t;
            t.type = kv.value(QStringLiteral("type"));
            t.streamIndex = static_cast<int>(parseInt(kv.value(QStringLiteral("streamIndex")), -1));
            t.packetId = static_cast<quint16>(parseInt(kv.value(QStringLiteral("packetId")), 0));
            t.componentTag = static_cast<int>(parseInt(kv.value(QStringLiteral("componentTag")), -1));
            t.samplingRate = static_cast<quint32>(parseInt(kv.value(QStringLiteral("rate")), 0));
            t.latm = parseInt(kv.value(QStringLiteral("latm")), 0) != 0;
            t.audioMode = static_cast<quint8>(parseInt(kv.value(QStringLiteral("audioMode")), 0));
            t.channels = static_cast<quint16>(parseInt(kv.value(QStringLiteral("channels")), 0));
            out.tracks.push_back(t);
            continue;
        }
        if (line.startsWith(QLatin1String("mpt "))) {
            const auto kv = parseKeyValues(line.mid(4));
            MmtsMptChange change;
            change.timeMs = parseInt(kv.value(QStringLiteral("time_ms")), -1);
            change.offset = static_cast<quint64>(parseInt(kv.value(QStringLiteral("offset")), 0));
            change.tracks += parseMptTrackList(kv.value(QStringLiteral("audio")), QStringLiteral("audio"));
            change.tracks += parseMptTrackList(kv.value(QStringLiteral("subtitle")), QStringLiteral("subtitle"));
            out.mptChanges.push_back(change);
            continue;
        }
        if (line.startsWith(QLatin1String("rap ")) || line.startsWith(QLatin1String("seek "))) {
            const bool isRap = line.startsWith(QLatin1String("rap "));
            const auto kv = parseKeyValues(line.mid(isRap ? 4 : 5));
            MmtsPoint p;
            p.timeMs = parseInt(kv.value(QStringLiteral("time_ms")), -1);
            p.offset = static_cast<quint64>(parseInt(kv.value(QStringLiteral("offset")), 0));
            if (isRap)
                out.rapPoints.push_back(p);
            else
                out.seekPoints.push_back(p);
            continue;
        }

        const int eq = line.indexOf(QLatin1Char('='));
        if (eq <= 0)
            continue;
        const QString key = line.left(eq);
        const QString value = line.mid(eq + 1);
        if (key == QLatin1String("source_size"))
            out.sourceSize = static_cast<quint64>(parseInt(value, 0));
        else if (key == QLatin1String("duration_ms"))
            out.durationMs = parseInt(value, 0);
        else if (key == QLatin1String("first_video_pts_ms"))
            out.firstVideoPtsMs = parseInt(value, -1);
        else if (key == QLatin1String("last_video_pts_ms"))
            out.lastVideoPtsMs = parseInt(value, -1);
    }

    out.ok = true;
    return out;
}

} // namespace

QString MmtsTrack::shortDesc() const
{
    return QStringLiteral("%1 #%2 pid=0x%3")
        .arg(type)
        .arg(streamIndex)
        .arg(packetId, 4, 16, QLatin1Char('0'));
}

static QString channelLabel(quint16 channels)
{
    switch (channels) {
    case 0:  return QString();              // unknown (v2 / text without ch)
    case 1:  return QStringLiteral("1.0ch");
    case 2:  return QStringLiteral("2.0ch");
    case 3:  return QStringLiteral("3ch");
    case 4:  return QStringLiteral("4ch");
    case 5:  return QStringLiteral("5ch");
    case 6:  return QStringLiteral("5.1ch");
    case 7:  return QStringLiteral("6.1ch");
    case 8:  return QStringLiteral("7.1ch");
    case 12: return QStringLiteral("11.2ch");
    case 24: return QStringLiteral("22.2ch");
    default: return QStringLiteral("%1ch").arg(channels);
    }
}

QString MmtsTrack::audioLabel() const
{
    QString s = QStringLiteral("A#%1 pid=0x%2 tag=%3")
                    .arg(streamIndex)
                    .arg(packetId, 4, 16, QLatin1Char('0'))
                    .arg(componentTag);
    if (samplingRate)
        s += QStringLiteral(" %1kHz").arg(samplingRate / 1000.0, 0, 'g', 4);
    const QString ch = channelLabel(channels);
    if (!ch.isEmpty())
        s += QStringLiteral(" %1").arg(ch);
    if (audioMode)
        s += QStringLiteral("(mode%1)").arg(audioMode);
    if (latm)
        s += QStringLiteral(" LATM");
    return s;
}

QString MmtsTrack::subtitleLabel() const
{
    return QStringLiteral("S#%1 pid=0x%2 tag=%3")
        .arg(streamIndex)
        .arg(packetId, 4, 16, QLatin1Char('0'))
        .arg(componentTag);
}

QString MmtsTrack::audioKey() const
{
    return QStringLiteral("%1:%2:%3:%4:%5")
        .arg(streamIndex)
        .arg(packetId)
        .arg(componentTag)
        .arg(audioMode)
        .arg(channels);
}

quint64 MmtsMapData::maxOffset() const
{
    quint64 m = sourceSize;
    for (const auto& p : rapPoints) m = std::max(m, p.offset);
    for (const auto& p : seekPoints) m = std::max(m, p.offset);
    for (const auto& c : mptChanges) m = std::max(m, c.offset);
    return m;
}

qint64 MmtsMapData::maxTimeMs() const
{
    qint64 m = std::max<qint64>(durationMs, 0);
    for (const auto& p : rapPoints) m = std::max(m, p.timeMs);
    for (const auto& p : seekPoints) m = std::max(m, p.timeMs);
    for (const auto& c : mptChanges) m = std::max(m, c.timeMs);
    return m;
}

qint64 MmtsMapData::baseTimeMs() const
{
    if (firstVideoPtsMs >= 0)
        return firstVideoPtsMs;
    qint64 m = std::numeric_limits<qint64>::max();
    bool any = false;
    auto consider = [&](qint64 t) { if (t >= 0) { m = std::min(m, t); any = true; } };
    for (const auto& p : rapPoints) consider(p.timeMs);
    for (const auto& p : seekPoints) consider(p.timeMs);
    for (const auto& c : mptChanges) consider(c.timeMs);
    return any ? m : 0;
}

qint64 MmtsMapData::spanMs() const
{
    const qint64 base = baseTimeMs();
    qint64 m = std::max<qint64>(durationMs, 0);
    auto rel = [&](qint64 t) { return t >= 0 ? t - base : 0; };
    for (const auto& p : rapPoints) m = std::max(m, rel(p.timeMs));
    for (const auto& p : seekPoints) m = std::max(m, rel(p.timeMs));
    for (const auto& c : mptChanges) m = std::max(m, rel(c.timeMs));
    return std::max<qint64>(m, 1);
}

QVector<MmtsAudioLane> audioLanesFrom(const MmtsMapData& data)
{
    QVector<MmtsAudioLane> lanes;
    if (data.mptChanges.isEmpty())
        return lanes;

    const qint64 base = data.baseTimeMs();
    const qint64 endAbs = base + data.spanMs();

    // MPT changes in chronological order (file order is usually already so).
    QVector<MmtsMptChange> changes = data.mptChanges;
    std::sort(changes.begin(), changes.end(),
              [](const MmtsMptChange& a, const MmtsMptChange& b) { return a.timeMs < b.timeMs; });

    QHash<QString, int> laneByKey; // audioKey -> index into lanes

    for (int k = 0; k < changes.size(); ++k) {
        const qint64 segStart = std::max<qint64>(changes[k].timeMs, base);
        const qint64 segEnd = (k + 1 < changes.size()) ? changes[k + 1].timeMs : endAbs;
        if (segEnd <= segStart)
            continue;
        for (const MmtsTrack& t : changes[k].tracks) {
            if (t.type != QLatin1String("audio"))
                continue;
            const QString key = t.audioKey();
            int idx = laneByKey.value(key, -1);
            if (idx < 0) {
                idx = lanes.size();
                laneByKey.insert(key, idx);
                MmtsAudioLane lane;
                lane.track = t;
                lane.label = t.audioLabel();
                lanes.push_back(lane);
            }
            MmtsAudioInterval iv{ segStart - base, segEnd - base };
            auto& ivs = lanes[idx].intervals;
            if (!ivs.isEmpty() && ivs.last().endMs >= iv.startMs)
                ivs.last().endMs = std::max(ivs.last().endMs, iv.endMs); // merge contiguous
            else
                ivs.push_back(iv);
        }
    }
    return lanes;
}

MmtsMapData MmtsMapFile::load(const QString& path)
{
    MmtsMapData out;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        out.error = QStringLiteral("Cannot open file: %1").arg(file.errorString());
        return out;
    }
    const qint64 size = file.size();
    if (size <= 0) {
        out.error = QStringLiteral("File is empty.");
        return out;
    }
    if (size > kMaxMapSize) {
        out.error = QStringLiteral("File is too large (%1 bytes).").arg(size);
        return out;
    }

    char magic[8] = {};
    const qint64 got = file.read(magic, sizeof(magic));
    const bool isV2 = got == 8 && std::memcmp(magic, "MMTSMAP2", 8) == 0;
    const bool isV3 = got == 8 && std::memcmp(magic, "MMTSMAP3", 8) == 0;

    if (isV2 || isV3) {
        const QByteArray buf = file.readAll(); // after the 8-byte magic
        Cursor cur(buf.constData(), buf.size());
        return parseBinary(cur, isV3, out);
    }

    return parseText(file, out);
}
