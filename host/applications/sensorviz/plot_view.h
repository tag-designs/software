/**
 * @file    plot_view.h
 * @brief   The sensorViz plot widget: draws a PlotSpec with QCustomPlot.
 *
 * @details PlotView is the only sensorViz code that touches QCustomPlot. It
 *          draws whatever PlotSpec it is given and keeps only view state of
 *          its own: the x range, the cursors and the metadata box position.
 *          Display policy lives in buildPlotSpec() (plot_spec.h), and menus,
 *          dialogs and readouts in MainWindow.
 *
 * @see     plot_spec.h, host/applications/sensorviz/README.md
 */
#ifndef SENSORVIZ_PLOT_VIEW_H
#define SENSORVIZ_PLOT_VIEW_H

#include <QPair>
#include <QPoint>
#include <QPointF>
#include <QSharedPointer>
#include <QVector>
#include <QWidget>

#include <qcustomplot.h>

#include "plot_spec.h"

class QPrinter;

/**
 * @class   PlotView
 * @brief   Draws a PlotSpec, with pan and zoom on time, cursors and a draggable metadata box.
 *
 * @details **Data.** Graphs never hold a stream's samples. After every
 *          QCustomPlot layout pass, and so on every pan, zoom and resize, the
 *          graphs are refilled with the M4 envelope of the visible window from
 *          the sensordata::Log given to setLog(), one column per device pixel:
 *          at most four points per pixel, drawing the same line as every
 *          sample. A refill happens only when the window or width changed.
 *
 *          **Spec changes.** setSpec() rebuilds graphs and axes, keeping the
 *          current x range, unless the new spec differs from the current one
 *          only in its title, UTC offset or gap mode, which are applied in
 *          place. A rebuild resets the cursors to the edges of the view, as it
 *          always has.
 *
 *          **Gaps.** With PlotSpec::breakAtGaps, lines break at gaps in the
 *          data: the envelope carries a NaN-valued gap marker between the two
 *          sides of each gap, which QCustomPlot draws as a break.
 *
 *          **Mouse.** Dragging and the wheel pan and zoom time only.
 *          Double-click places the left cursor, shift-double-click the right
 *          one, showing the cursors if they were hidden. The metadata box can
 *          be dragged; its position is session state.
 *
 *          Must be used from the GUI thread.
 */
class PlotView : public QWidget
{
    Q_OBJECT

public:
    /**
     * @brief   Creates an empty plot showing the epoch time axis.
     * @param[in] parent  Owning widget.
     */
    explicit PlotView(QWidget *parent = nullptr);

    /**
     * @brief   Sets the plot data that trace StreamIds refer to.
     *
     * @details Clears every graph, since their StreamIds named streams of the
     *          previous log, and forgets the current spec, so the next
     *          setSpec() is a full rebuild that zooms to the data.
     *
     * @param[in] log  The loaded log's plot data, or null. Not owned; must
     *                 outlive its use here, until the next setLog().
     */
    void setLog(const sensordata::Log *log);

    /**
     * @brief   Shows @p spec. See the class description for what is rebuilt.
     *
     * @details With no previous graphs, the x range zooms to the spec's data
     *          range as resetZoom() does. Replots before returning.
     *
     * @param[in] spec  What to show; its StreamIds must name streams of the
     *                  Log given to setLog().
     */
    void setSpec(const PlotSpec &spec);

    /// The spec being shown.
    const PlotSpec &spec() const { return spec_; }

    /**
     * @brief   Zooms the time axis to the spec's data range plus 5%, and resets the cursors.
     * @note    Does nothing when no trace is shown.
     */
    void resetZoom();

    /// Zooms the time axis to the interval between the cursors, when they are shown and ordered.
    void zoomToCursors();

    /**
     * @brief   Shows the cursors at the edges of the view, or hides them.
     * @param[in] visible  Whether to show them.
     */
    void setCursorsVisible(bool visible);

    /// Current time axis range, seconds in the spec's time domain.
    QPair<double, double> xRange() const;

    /**
     * @brief   Draws the plot onto a printer at the printer's resolution.
     *
     * @details Paints with QCustomPlot::toPainter() for vector output, in the
     *          screen's coordinate system so text keeps its size: the
     *          printer's resolution is changed to the screen's logical DPI
     *          before painting begins. While drawing, graphs are refilled with
     *          one envelope column per printer device pixel and QCustomPlot's
     *          adaptive sampling is off, so the printed lines are as exact as
     *          the screen's. The screen envelope is restored on the next
     *          replot, which is queued.
     *
     * @param[in,out] printer     Printer or preview device; painting must not
     *                            have begun. Its resolution is changed.
     * @param[in]     device_dpi  The printer's real resolution, before any
     *                            earlier call changed it; 0 to use the
     *                            printer's current resolution.
     */
    void render(QPrinter *printer, int device_dpi);

signals:
    /**
     * @brief   The mouse moved over the data area.
     * @param[in] time  Time under the mouse, seconds in the spec's domain.
     */
    void hoverTime(double time);

    /**
     * @brief   A cursor was placed by double-click.
     * @param[in] left   Left cursor time.
     * @param[in] right  Right cursor time.
     */
    void cursorsChanged(double left, double right);

    /**
     * @brief   The cursors were shown because the user double-clicked while they were hidden.
     * @param[in] visible  Always true at present.
     */
    void cursorsVisibleChanged(bool visible);

    /**
     * @brief   The time axis range changed (pan, zoom, rebuild or reset).
     * @param[in] lower  New lower bound.
     * @param[in] upper  New upper bound.
     */
    void xRangeChanged(double lower, double upper);

    /**
     * @brief   The user asked for the plot's context menu.
     * @param[in] globalPos  Where, in global coordinates.
     */
    void contextMenuRequested(const QPoint &globalPos);

private:
    QCPAxis *timeAxis() const;
    void applyTimeAxis();
    void applyTitleAndMetadata();
    void rebuild();
    void resetXRangeToData();
    QCPAxis *addValueAxis(SensorAxisSide side, int side_index, const PlotAxisSpec &spec);
    void clearDynamicAxes();
    void clearEventMarkers();
    void addEventMarkers();
    void resetCursorsToView();
    int plotColumns(double device_pixels_per_pixel) const;
    void fillGraphs(double lower, double upper, int columns);
    void refillAfterLayout();
    void mousePress(QMouseEvent *event);
    void mouseMove(QMouseEvent *event);
    void mouseRelease(QMouseEvent *event);
    void mouseDoubleClick(QMouseEvent *event);

    QCustomPlot *plot_ = nullptr;              ///< The plot; owned through Qt parenting.
    const sensordata::Log *log_ = nullptr;     ///< Plot data for trace StreamIds; not owned.
    PlotSpec spec_;                            ///< What is shown.
    bool has_spec_ = false;                    ///< False until the first setSpec() after setLog().

    QSharedPointer<QCPAxisTickerDateTime> date_ticker_; ///< Epoch tick labels, in the spec's UTC offset.
    QCPAxis *elapsed_x_axis_ = nullptr;        ///< Time axis for elapsed-time logs.
    QVector<QCPAxis *> dynamic_axes_;          ///< Value axes beyond the built-in left and right ones.
    QVector<QCPItemStraightLine *> event_marker_items_; ///< One per event marker time.
    QCPItemLine *left_cursor_ = nullptr;       ///< Left time cursor.
    QCPItemLine *right_cursor_ = nullptr;      ///< Right time cursor.
    bool cursors_enabled_ = true;              ///< Whether rebuilds show the cursors.
    QCPTextElement *title_ = nullptr;          ///< Title row above the axis rect.
    QCPItemText *metadata_box_ = nullptr;      ///< Contextual constants overlay.

    bool metadata_box_dragging_ = false;       ///< A metadata box drag is in progress.
    QPoint metadata_box_drag_start_pixel_;     ///< Mouse position where the drag began.
    QPointF metadata_box_drag_start_coords_;   ///< Box position, in axis-rect ratios, where the drag began.

    QVector<QPair<QCPGraph *, sensordata::StreamId>> graphs_; ///< Graphs filled from the log, with their streams.
    double fill_lower_ = 0.0;                  ///< Window of the last fill.
    double fill_upper_ = 0.0;                  ///< Window of the last fill.
    int fill_columns_ = 0;                     ///< Column count of the last fill.
    bool fill_valid_ = false;                  ///< False when the next layout pass must refill.
    double print_pixel_scale_ = 0.0;           ///< Printer device pixels per plot pixel while render() draws; 0 otherwise.
};

#endif
