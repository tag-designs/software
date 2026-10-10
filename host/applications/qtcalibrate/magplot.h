#ifndef MAGPLOT_H
#define MAGPLOT_H

#include <QFrame>
#include <QPaintEvent>
#include <QVector3D>
#include <QList>
#include <QQuaternion>

// magPlot draws the calibrated magnetometer cloud during calibration. It is a
// lightweight custom widget, separate from the QML compass/attitude displays,
// because it visualizes solver input coverage rather than tag orientation.
class magPlot : public QWidget
{
    Q_OBJECT
public:
    explicit magPlot(QWidget *parent = nullptr);
    void setField(float f);
    void setZoom(float z);
    void setFocusQuaternion(QQuaternion);
    void setPoints(QList<QVector3D>);
    void addPoint(QVector3D);
    void reset();

signals:

protected:

    void paintEvent(QPaintEvent *event) override;

    // override mouse events

    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:

    const float centerRadius = 2.0;
    const float dataPointSize = 0.5;
    const float highlightSize = 2.0;
    const float axisPointSize = 1.0;
    const float axisFontPixelSize = 4.0;

    /// Field, in uT, that the view is scaled to: the radius a sphere of this
    /// strength is drawn at fills the frame the way the old fixed sphere did.
    /// The drawn sphere is at the computed field, not at this, so the two
    /// differ exactly as the site's field differs from a nominal 50 uT.
    /// Earth's ranges over about 25 to 65, so the sphere varies by some 2.6x
    /// across sites; the wheel zooms either way.
    static constexpr float kViewField = 50.0f;

    /// Shown until the solver returns a field of its own. The same nominal
    /// value the solver starts its own arithmetic from.
    static constexpr float kDefaultField = 50.0f;

    float field; // computed magnetic field, uT -- the drawn sphere's radius
    float zoom ; // zoom factor set by wheel


    QList<QVector3D> points; // data to plot
    QQuaternion focusQ;      // rotation to focal point
    QQuaternion savedQ;      // rotation of latest data
    QQuaternion rotationQ;   // added rotation through mouse or setFocusQuaternion

    // temporary state for mouse tracking

    QQuaternion old_rotationQ;
    bool m_isDragging = false;
    QPointF m_lastPos;


    // drawing helpers

    void drawAxis(QPainter *e, QVector3D pt, QColor color, QString& text);

    // if resize is true, points with -z are resized
    void drawPoint(QPainter *p, QVector3D pt, QColor color, float size);

};

#endif // MAGPLOT_H
