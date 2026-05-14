#ifndef SENSORVIZ_MAINWINDOW_H
#define SENSORVIZ_MAINWINDOW_H

#include <QAction>
#include <QLabel>
#include <QMainWindow>
#include <QMap>
#include <QMenu>
#include <QPrinter>
#include <QSharedPointer>
#include <QTabWidget>
#include <QTextEdit>
#include <QVector>

#include <qcustomplot.h>

#include "sensorstream.h"

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

private slots:
    void loadLog();
    void showAbout();
    void resetZoom();
    void setUtcOffset();
    void altitudeToggled(bool checked);
    void activityFilterToggled(bool checked);
    void setPressureRange();
    void printPlot();
    void renderPlot(QPrinter *printer);
    void zoomToCursors();
    void plotDoubleClick(QMouseEvent *event);
    void showPlotContextMenu(const QPoint &pos);
    void showMousePosition(QMouseEvent *event);

private:
    void createUi();
    void createActions();
    void updateMetadata();
    void updateTransformActions();
    void addOrReplaceStream(const SensorStream &stream, bool checked);
    void removeStream(const QString &id);
    void clearStreamActions();
    void addStreamAction(const SensorStream &stream, bool checked);
    QAction *streamActionById(const QString &id) const;
    bool hasStream(const QString &id) const;
    const SensorStream *streamById(const QString &id) const;
    QVector<SensorStream> visibleStreams() const;
    void refreshPlot();
    void clearDynamicAxes();
    QCPAxis *axisForStream(int visible_index, const SensorStream &stream);
    QString streamValueAt(const SensorStream &stream, double epoch) const;
    void resetCursorsToView();

    SensorLog log_;
    QVector<SensorStream> streams_;
    QString current_path_;
    int utc_offset_ = 0;
    double sea_level_pressure_ = 1013.25;
    double activity_low_pass_seconds_ = 600.0;
    QMap<QString, QCPRange> custom_axis_ranges_;

    QWidget *central_ = nullptr;
    QCustomPlot *plot_ = nullptr;
    QTextEdit *info_ = nullptr;
    QLabel *status_ = nullptr;
    QTabWidget *tabs_ = nullptr;

    QAction *load_action_ = nullptr;
    QAction *about_action_ = nullptr;
    QAction *reset_action_ = nullptr;
    QAction *utc_offset_action_ = nullptr;
    QAction *altitude_action_ = nullptr;
    QAction *activity_filter_action_ = nullptr;
    QAction *pressure_range_action_ = nullptr;
    QAction *print_action_ = nullptr;
    QAction *zoom_to_cursors_action_ = nullptr;
    QAction *view_stream_separator_ = nullptr;
    QMenu *view_menu_ = nullptr;
    QMenu *configuration_menu_ = nullptr;
    QVector<QAction *> stream_actions_;

    QSharedPointer<QCPAxisTickerDateTime> date_ticker_;
    QVector<QCPAxis *> dynamic_axes_;
    QCPItemLine *left_cursor_ = nullptr;
    QCPItemLine *right_cursor_ = nullptr;
};

#endif
