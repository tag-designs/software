#ifndef MAGPLOT_H
#define MAGPLOT_H

#include <QFrame>
#include <QPaintEvent>
#include <QVector3D>
#include <QList>
#include <QQuaternion>

// magPlot draws one sensor's direction cloud during calibration. It is a
// lightweight custom widget, separate from the QML compass/attitude displays,
// because it visualizes solver input coverage rather than tag orientation.
//
// It holds both clouds and draws one. The two calibrations want different
// motions -- the magnetometer needs the field to point in many directions in
// the tag frame, the accelerometer needs gravity to, and each is blind to
// rotation about the direction it measures -- so the operator is asked for
// one at a time and shown the cloud that answers it. Both accumulate
// throughout regardless of which is on screen.
class magPlot : public QWidget
{
    Q_OBJECT
public:
    /// Which cloud is drawn. Both are collected either way.
    enum class Source {
        MagneticField,   ///< Calibrated magnetometer vectors.
        Gravity,         ///< Accelerometer vectors, offset removed.
    };

    explicit magPlot(QWidget *parent = nullptr);
    void setSource(Source source);
    Source source() const { return source_; }

    void setField(float f);

    /// Reference radius for the gravity cloud, in the caller's units. Points
    /// are drawn at their true magnitude against it, so a loose fit shows as
    /// a thick shell exactly as it does for the field.
    void setGravityRadius(float r);
    void setZoom(float z);
    void setFocusQuaternion(QQuaternion);
    void setPoints(QList<QVector3D>);
    void addPoint(QVector3D);
    void addGravityPoint(QVector3D);
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

    float field;          // magnetic field magnitude, the field cloud's radius
    float gravityRadius;  // one g in the caller's units, the gravity cloud's
    float zoom ;          // zoom factor set by wheel

    Source source_ = Source::MagneticField;

    QList<QVector3D> points;        // magnetometer cloud
    QList<QVector3D> gravityPoints; // accelerometer cloud
    QQuaternion focusQ;      // rotation to focal point
    QQuaternion savedQ;      // rotation of latest data
    QQuaternion rotationQ;   // added rotation through mouse or setFocusQuaternion

    // temporary state for mouse tracking

    QQuaternion old_rotationQ;
    bool m_isDragging = false;
    QPointF m_lastPos;


    // drawing helpers

    /// The cloud and radius currently selected.
    const QList<QVector3D> &activePoints() const;
    float activeRadius() const;
    QString caption() const;

    void drawAxis(QPainter *e, QVector3D pt, QColor color, QString& text);

    // if resize is true, points with -z are resized
    void drawPoint(QPainter *p, QVector3D pt, QColor color, float size);

};

#endif // MAGPLOT_H
