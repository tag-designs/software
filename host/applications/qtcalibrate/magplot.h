#ifndef MAGPLOT_H
#define MAGPLOT_H

#include <QFrame>
#include <QPaintEvent>
#include <QVector3D>
#include <QList>
#include <QQuaternion>
#include <QString>
#include <QVector>

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
        MagneticField,   ///< Calibrated magnetometer cloud.
        Gravity,         ///< Accelerometer cloud, offset removed.
        Poses,           ///< The six orientations, as a cube of faces to fill.
    };

    explicit magPlot(QWidget *parent = nullptr);
    void setSource(Source source);
    Source source() const { return source_; }

    /// Text drawn over the cloud. The widget draws what it is told rather
    /// than naming the cloud itself: what is worth saying -- which sensor,
    /// how far along it is -- is the calibration's business, not the plot's.
    void setCaption(const QString &text);

    /**
     * @brief Which of the six faces are filled, and which is being held.
     *
     * @param done     One flag per face, in PoseCheck's order.
     * @param holding  The face being held now, or -1.
     *
     * @details Only drawn in Source::Poses. A cube says what a direction
     *          cloud cannot: which orientations are left. Its faces are the
     *          ones a board actually rests on, so a hollow face is an
     *          instruction on its own -- turn the tag so that one points
     *          down -- with no wording to get right for a given enclosure.
     */
    void setPoses(const QVector<bool> &done, int holding);


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

    /*
     * Sizes as fractions of the sphere's own radius, not as absolute lengths.
     *
     * They used to be absolute, which worked only while there was one cloud:
     * the field is tens of microtesla, so a 0.5 unit dot on a 45 unit sphere
     * reads well. The gravity cloud has a radius of a thousand, and the same
     * absolute dot comes out twenty times smaller than a pixel -- the sphere
     * drew, with nothing on it.
     *
     * The divisor is the default field, so the magnetometer cloud keeps the
     * proportions it has always had and the gravity cloud now matches it.
     */
    static constexpr float kReferenceField = 60.0f;
    const float centerFraction    = 2.0f / kReferenceField;
    const float dataPointFraction = 0.5f / kReferenceField;
    const float highlightFraction = 2.0f / kReferenceField;
    const float axisPointFraction = 1.0f / kReferenceField;
    const float axisFontFraction  = 4.0f / kReferenceField;
    const float strokeFraction    = 0.1f / kReferenceField;
    /// Half-edge of the pose cube, as a fraction of the sphere radius. Short
    /// of the sphere so the cube sits inside the same frame the clouds use.
    const float cubeFraction      = 0.62f;

    float field;          // magnetic field magnitude, the field cloud's radius
    float gravityRadius;  // one g in the caller's units, the gravity cloud's
    float zoom ;          // zoom factor set by wheel

    Source source_ = Source::MagneticField;

    QString caption_;               // drawn over the cloud
    QVector<bool> poseDone;         // one per cube face
    int poseHolding = -1;           // face being held, or -1
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

    void drawAxis(QPainter *e, QVector3D pt, QColor color, QString& text);
    void drawPoseCube(QPainter *p, float radius);

    // if resize is true, points with -z are resized
    void drawPoint(QPainter *p, QVector3D pt, QColor color, float size);

};

#endif // MAGPLOT_H
