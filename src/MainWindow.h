#pragma once

#include "MmtsMapFile.h"
#include <QMainWindow>

class QLabel;
class QTableWidget;
class QTabWidget;
class TimelineView;
class AudioLanesView;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

    void loadFile(const QString& path);

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private slots:
    void openFile();

private:
    void buildUi();
    void populate(const MmtsMapData& data);
    void clearViews();

    QLabel* m_summary = nullptr;
    TimelineView* m_timeline = nullptr;
    AudioLanesView* m_audioLanes = nullptr;
    QTabWidget* m_tabs = nullptr;
    QTableWidget* m_tracksTable = nullptr;
    QTableWidget* m_mptTable = nullptr;
    QTableWidget* m_rapTable = nullptr;
    QTableWidget* m_seekTable = nullptr;

    QString m_currentPath;
};
