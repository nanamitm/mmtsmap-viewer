#include "MainWindow.h"
#include "AudioLanesView.h"
#include "TimelineView.h"

#include <QScrollArea>

#include <QApplication>
#include <QColor>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QSplitter>
#include <QStatusBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QWidget>

namespace {

QString msToClock(qint64 ms)
{
    if (ms < 0)
        return QStringLiteral("-");
    const qint64 totalMs = ms % 1000;
    const qint64 totalSec = ms / 1000;
    return QStringLiteral("%1:%2:%3.%4")
        .arg(totalSec / 3600, 2, 10, QLatin1Char('0'))
        .arg((totalSec % 3600) / 60, 2, 10, QLatin1Char('0'))
        .arg(totalSec % 60, 2, 10, QLatin1Char('0'))
        .arg(totalMs, 3, 10, QLatin1Char('0'));
}

QString bytesHuman(quint64 bytes)
{
    double b = static_cast<double>(bytes);
    const char* units[] = { "B", "KiB", "MiB", "GiB", "TiB" };
    int u = 0;
    while (b >= 1024.0 && u < 4) { b /= 1024.0; ++u; }
    return QStringLiteral("%1 %2 (%3)")
        .arg(b, 0, 'f', u == 0 ? 0 : 2)
        .arg(QLatin1String(units[u]))
        .arg(QLocale().toString(static_cast<qlonglong>(bytes)));
}

QTableWidgetItem* item(const QString& text, bool numeric = false)
{
    auto* it = new QTableWidgetItem(text);
    it->setFlags(it->flags() & ~Qt::ItemIsEditable);
    if (numeric)
        it->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    return it;
}

QTableWidget* makeTable(const QStringList& headers)
{
    auto* t = new QTableWidget;
    t->setColumnCount(headers.size());
    t->setHorizontalHeaderLabels(headers);
    t->setEditTriggers(QAbstractItemView::NoEditTriggers);
    t->setSelectionBehavior(QAbstractItemView::SelectRows);
    t->setAlternatingRowColors(true);
    t->verticalHeader()->setDefaultSectionSize(20);
    t->horizontalHeader()->setStretchLastSection(true);
    return t;
}

} // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent)
{
    buildUi();
    setAcceptDrops(true);
    setWindowTitle(tr("mmtsmap Viewer"));
    resize(960, 720);
    statusBar()->showMessage(tr("Open a .mmtsmap file (File > Open, or drag & drop)."));
}

void MainWindow::buildUi()
{
    auto* fileMenu = menuBar()->addMenu(tr("&File"));
    auto* openAct = fileMenu->addAction(tr("&Open..."));
    openAct->setShortcut(QKeySequence::Open);
    connect(openAct, &QAction::triggered, this, &MainWindow::openFile);
    fileMenu->addSeparator();
    auto* quitAct = fileMenu->addAction(tr("&Quit"));
    quitAct->setShortcut(QKeySequence::Quit);
    connect(quitAct, &QAction::triggered, this, &QWidget::close);

    auto* central = new QWidget;
    auto* layout = new QVBoxLayout(central);
    layout->setContentsMargins(8, 8, 8, 8);

    m_summary = new QLabel;
    m_summary->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_summary->setWordWrap(true);
    m_summary->setStyleSheet(QStringLiteral(
        "QLabel { background:#2a2d35; color:#dfe3ea; padding:8px; border-radius:4px; }"));
    layout->addWidget(m_summary);

    auto* splitter = new QSplitter(Qt::Vertical);

    m_timeline = new TimelineView;
    splitter->addWidget(m_timeline);

    m_tabs = new QTabWidget;

    // Audio-track activity lanes (scrollable for many tracks).
    m_audioLanes = new AudioLanesView;
    auto* laneScroll = new QScrollArea;
    laneScroll->setWidget(m_audioLanes);
    laneScroll->setWidgetResizable(true);
    laneScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    m_tracksTable = makeTable({ tr("Type"), tr("Stream"), tr("PacketId"), tr("CompTag"),
                                tr("Rate"), tr("LATM"), tr("AudioMode"), tr("Ch") });
    m_mptTable = makeTable({ tr("Time"), tr("rel ms"), tr("Offset"),
                             tr("Audio"), tr("Subtitle"), tr("Change") });
    m_rapTable = makeTable({ tr("#"), tr("Time"), tr("rel ms"), tr("Offset (bytes)") });
    m_seekTable = makeTable({ tr("#"), tr("Time"), tr("rel ms"), tr("Offset (bytes)") });
    m_tabs->addTab(laneScroll, tr("Audio Timeline"));
    m_tabs->addTab(m_tracksTable, tr("Tracks"));
    m_tabs->addTab(m_mptTable, tr("MPT Changes"));
    m_tabs->addTab(m_rapTable, tr("RAP Points"));
    m_tabs->addTab(m_seekTable, tr("Seek Points"));
    splitter->addWidget(m_tabs);

    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 2);
    layout->addWidget(splitter, 1);

    setCentralWidget(central);
    clearViews();
}

void MainWindow::openFile()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Open .mmtsmap"), m_currentPath,
        tr("MMTS map (*.mmtsmap);;All files (*.*)"));
    if (!path.isEmpty())
        loadFile(path);
}

void MainWindow::loadFile(const QString& path)
{
    const MmtsMapData data = MmtsMapFile::load(path);
    if (!data.ok) {
        QMessageBox::warning(this, tr("Failed to load"),
                             tr("Could not parse \"%1\":\n%2").arg(path, data.error));
        statusBar()->showMessage(tr("Failed: %1").arg(data.error));
        return;
    }
    m_currentPath = path;
    setWindowTitle(tr("mmtsmap Viewer — %1").arg(QFileInfo(path).fileName()));
    populate(data);
    statusBar()->showMessage(tr("Loaded %1 — %2").arg(QFileInfo(path).fileName(), data.formatName));
}

void MainWindow::clearViews()
{
    m_summary->setText(tr("<i>No file loaded.</i>"));
    if (m_timeline)
        m_timeline->clear();
    if (m_audioLanes)
        m_audioLanes->clear();
    for (auto* t : { m_tracksTable, m_mptTable, m_rapTable, m_seekTable })
        if (t)
            t->setRowCount(0);
}

void MainWindow::populate(const MmtsMapData& data)
{
    // --- summary -----------------------------------------------------------
    const qint64 derivedDur = (data.firstVideoPtsMs >= 0 && data.lastVideoPtsMs >= data.firstVideoPtsMs)
        ? data.lastVideoPtsMs - data.firstVideoPtsMs : data.durationMs;
    m_summary->setText(tr(
        "<b>Format:</b> %1 &nbsp;&nbsp; <b>Source size:</b> %2<br>"
        "<b>Duration:</b> %3 (%4 ms) &nbsp;&nbsp; "
        "<b>First/Last video PTS:</b> %5 / %6 ms<br>"
        "<b>Tracks:</b> %7 &nbsp;&nbsp; <b>MPT changes:</b> %8 &nbsp;&nbsp; "
        "<b>RAP:</b> %9 &nbsp;&nbsp; <b>Seek:</b> %10")
        .arg(data.formatName)
        .arg(bytesHuman(data.sourceSize))
        .arg(msToClock(derivedDur))
        .arg(data.durationMs)
        .arg(data.firstVideoPtsMs)
        .arg(data.lastVideoPtsMs)
        .arg(data.tracks.size())
        .arg(data.mptChanges.size())
        .arg(data.rapPoints.size())
        .arg(data.seekPoints.size()));

    m_timeline->setData(data);
    m_audioLanes->setData(data);

    const qint64 base = data.baseTimeMs();

    // --- tracks ------------------------------------------------------------
    m_tracksTable->setRowCount(data.tracks.size());
    for (int i = 0; i < data.tracks.size(); ++i) {
        const MmtsTrack& t = data.tracks[i];
        const bool audio = t.type == QLatin1String("audio");
        m_tracksTable->setItem(i, 0, item(t.type));
        m_tracksTable->setItem(i, 1, item(QString::number(t.streamIndex), true));
        m_tracksTable->setItem(i, 2, item(QStringLiteral("0x%1").arg(t.packetId, 4, 16, QLatin1Char('0'))));
        m_tracksTable->setItem(i, 3, item(QString::number(t.componentTag), true));
        m_tracksTable->setItem(i, 4, item(audio ? QString::number(t.samplingRate) : QString(), true));
        m_tracksTable->setItem(i, 5, item(audio ? (t.latm ? tr("yes") : tr("no")) : QString()));
        m_tracksTable->setItem(i, 6, item(audio ? QString::number(t.audioMode) : QString(), true));
        m_tracksTable->setItem(i, 7, item(audio && t.channels ? QString::number(t.channels) : QString(), true));
    }
    m_tracksTable->resizeColumnsToContents();

    // --- mpt changes (audio / subtitle split + change marking) -------------
    m_mptTable->setRowCount(data.mptChanges.size());
    QMap<QString, QString> prevAudio, prevSub; // key -> label, from previous row
    for (int i = 0; i < data.mptChanges.size(); ++i) {
        const MmtsMptChange& c = data.mptChanges[i];
        QStringList audioLabels, subLabels;
        QMap<QString, QString> curAudio, curSub;
        for (const auto& t : c.tracks) {
            if (t.type == QLatin1String("audio")) {
                audioLabels << t.audioLabel();
                curAudio.insert(t.audioKey(), t.audioLabel());
            } else if (t.type == QLatin1String("subtitle")) {
                subLabels << t.subtitleLabel();
                curSub.insert(t.subtitleLabel(), t.subtitleLabel());
            }
        }

        // Diff against the previous change to describe what happened.
        QStringList changeBits;
        if (i == 0) {
            changeBits << tr("initial");
        } else {
            auto diff = [&](const QMap<QString, QString>& cur, const QMap<QString, QString>& prev,
                            const QString& tag) {
                for (auto it = cur.begin(); it != cur.end(); ++it)
                    if (!prev.contains(it.key()))
                        changeBits << QStringLiteral("+%1 %2").arg(tag, it.value());
                for (auto it = prev.begin(); it != prev.end(); ++it)
                    if (!cur.contains(it.key()))
                        changeBits << QStringLiteral("−%1 %2").arg(tag, it.value());
            };
            diff(curAudio, prevAudio, tr("audio"));
            diff(curSub, prevSub, tr("sub"));
            if (changeBits.isEmpty())
                changeBits << tr("(no track change)");
        }
        prevAudio = curAudio;
        prevSub = curSub;

        const qint64 rel = c.timeMs >= 0 ? c.timeMs - base : -1;
        const QString changeText = changeBits.join(QStringLiteral("  "));
        auto* changeItem = item(changeText);
        const bool realChange = i > 0 && changeText != tr("(no track change)");
        if (realChange)
            changeItem->setForeground(QColor(0xff, 0xc8, 0x66));
        m_mptTable->setItem(i, 0, item(msToClock(rel)));
        m_mptTable->setItem(i, 1, item(QString::number(rel), true));
        m_mptTable->setItem(i, 2, item(bytesHuman(c.offset), true));
        m_mptTable->setItem(i, 3, item(audioLabels.join(QStringLiteral("\n"))));
        m_mptTable->setItem(i, 4, item(subLabels.join(QStringLiteral("\n"))));
        m_mptTable->setItem(i, 5, changeItem);
    }
    m_mptTable->resizeColumnsToContents();
    m_mptTable->resizeRowsToContents();

    // --- rap / seek --------------------------------------------------------
    auto fillPoints = [base](QTableWidget* table, const QVector<MmtsPoint>& pts) {
        table->setRowCount(pts.size());
        for (int i = 0; i < pts.size(); ++i) {
            const qint64 rel = pts[i].timeMs >= 0 ? pts[i].timeMs - base : -1;
            table->setItem(i, 0, item(QString::number(i), true));
            table->setItem(i, 1, item(msToClock(rel)));
            table->setItem(i, 2, item(QString::number(rel), true));
            table->setItem(i, 3, item(bytesHuman(pts[i].offset), true));
        }
        table->resizeColumnsToContents();
    };
    fillPoints(m_rapTable, data.rapPoints);
    fillPoints(m_seekTable, data.seekPoints);

    const int audioTrackCount = static_cast<int>(audioLanesFrom(data).size());
    m_tabs->setTabText(0, tr("Audio Timeline (%1)").arg(audioTrackCount));
    m_tabs->setTabText(1, tr("Tracks (%1)").arg(data.tracks.size()));
    m_tabs->setTabText(2, tr("MPT Changes (%1)").arg(data.mptChanges.size()));
    m_tabs->setTabText(3, tr("RAP Points (%1)").arg(data.rapPoints.size()));
    m_tabs->setTabText(4, tr("Seek Points (%1)").arg(data.seekPoints.size()));
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event)
{
    if (event->mimeData()->hasUrls())
        event->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent* event)
{
    const auto urls = event->mimeData()->urls();
    if (!urls.isEmpty())
        loadFile(urls.first().toLocalFile());
}
