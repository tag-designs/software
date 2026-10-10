/*
 *  Qt quaternions are in ENU order
 *  Sensors are in NED order.  To convert from ENU to NED
 *     Qenu(w,x,y,z) -> Qned(w,y,x,-z)
 *
 *
 */

#include "magplot.h"
#include <algorithm>

#include <QFont>
#include <QPainter>
#include <QPen>
#include <QPolygonF>
#include <random>
//#include <QtMinMax>

/*
static void ENU_NED(QQuaternion &qt)
{
    //qt = QQuaternion(qt.scalar(),qt.y(),qt.x(),-qt.z());
}
*/

magPlot::magPlot(QWidget *parent) : QWidget{parent}
{
    setAutoFillBackground(true);
    reset();
}

void magPlot::reset(){
    // QList::empty() is a const query whose result was discarded, so the
    // sample points survived every reset. clear() is the one that empties it.
    // Both clouds, or the second one survives a clear exactly as the first
    // used to.
    points.clear();
    gravityPoints.clear();
    caption_.clear();
    poseDone.clear();
    poseHolding = -1;
    source_ = Source::MagneticField;
    field = 60.0;          // default field
    gravityRadius = 1000.0;  // one g in calibration stream units
    zoom = 0.8;
    focusQ = QQuaternion(1.0,0.0,0.0,0.0);
    savedQ = QQuaternion(1.0,0.0,0.0,0.0);
    rotationQ = QQuaternion(1.0,0.0,0.0,0.0);
    update();
}

// change magnetic field
void magPlot::setField(float f)
{
    field = f;
    update();
}

void magPlot::setGravityRadius(float r)
{
    if (r > 0.0f) {
        gravityRadius = r;
        update();
    }
}

void magPlot::setSource(Source source)
{
    if (source_ == source) {
        return;
    }
    source_ = source;
    // The view follows the newest point of whichever cloud is shown, and the
    // two were last added at different orientations, so re-aim at the one now
    // on screen rather than leaving the camera pointed at the other's.
    const QList<QVector3D> &shown = activePoints();
    if (!shown.isEmpty()) {
        savedQ = QQuaternion::rotationTo(shown.last(), QVector3D(0, 0, -1));
        focusQ = rotationQ * savedQ;
    }
    update();
}

const QList<QVector3D> &magPlot::activePoints() const
{
    return (source_ == Source::MagneticField) ? points : gravityPoints;
}

float magPlot::activeRadius() const
{
    return (source_ == Source::MagneticField) ? field : gravityRadius;
}

void magPlot::setPoses(const QVector<bool> &done, int holding)
{
    if (poseDone != done || poseHolding != holding) {
        poseDone = done;
        poseHolding = holding;
        update();
    }
}

/**
 * @brief Draw the six faces, filled where the pose is done.
 *
 * @details Faces are sorted by depth and painted back to front, so the near
 *          ones cover the far ones and the solid shows as a solid. The view
 *          rotation already follows the newest gravity reading, so the face
 *          the tag is resting on is the one turned towards the operator, and
 *          a hollow face they can see is one to turn towards.
 *
 *          Face order matches PoseCheck: x down, x up, y down, y up, z down,
 *          z up. A face's outward normal is the direction the accelerometer
 *          reads when that face is down, which is what makes the two agree
 *          without either knowing about the other.
 */
void magPlot::drawPoseCube(QPainter *p, float radius)
{
    const float h = cubeFraction * radius;

    // Outward normals, in PoseCheck's order, each with two in-plane edges.
    struct Face { QVector3D normal, u, v; };
    static const Face faces[6] = {
        {{-1, 0, 0}, {0, 1, 0}, {0, 0, 1}},
        {{ 1, 0, 0}, {0, 1, 0}, {0, 0, 1}},
        {{ 0,-1, 0}, {1, 0, 0}, {0, 0, 1}},
        {{ 0, 1, 0}, {1, 0, 0}, {0, 0, 1}},
        {{ 0, 0,-1}, {1, 0, 0}, {0, 1, 0}},
        {{ 0, 0, 1}, {1, 0, 0}, {0, 1, 0}},
    };

    struct Drawn { float depth; int index; QPolygonF shape; };
    QVector<Drawn> drawn;
    drawn.reserve(6);

    for (int i = 0; i < 6; i++) {
        const Face &f = faces[i];
        QPolygonF shape;
        float depth = 0.0f;
        for (int corner = 0; corner < 4; corner++) {
            const float su = (corner == 0 || corner == 3) ? -1.0f : 1.0f;
            const float sv = (corner < 2) ? -1.0f : 1.0f;
            QVector3D pt = focusQ.rotatedVector(
                (f.normal + f.u * su + f.v * sv) * h);
            pt.setY(-pt.y());
            depth += pt.z();
            shape << QPointF(pt.x(), -pt.y());
        }
        drawn.append({depth / 4.0f, i, shape});
    }

    // Painter's algorithm: z grows away from the viewer here, so the most
    // positive depth is furthest and goes down first.
    std::sort(drawn.begin(), drawn.end(),
              [](const Drawn &a, const Drawn &b) { return a.depth > b.depth; });

    for (const Drawn &face : drawn) {
        const bool done = face.index < poseDone.size() && poseDone.at(face.index);
        const bool holding = (face.index == poseHolding);
        QColor fill = done ? QColor(70, 150, 90, 200) : QColor(235, 235, 235, 70);
        if (holding && !done) {
            fill = QColor(220, 150, 40, 170);
        }
        p->setBrush(QBrush(fill));
        p->setPen(QPen(done ? QColor(40, 100, 60) : QColor(150, 150, 150),
                       strokeFraction * radius * (holding ? 4.0f : 2.0f)));
        p->drawPolygon(face.shape);
    }
}

void magPlot::setCaption(const QString &text)
{
    if (caption_ != text) {
        caption_ = text;
        update();
    }
}

// set rotation to focus
void magPlot::setFocusQuaternion(QQuaternion fq)
{
    // save external rotation

    rotationQ = fq;

    // update composite rotation

    focusQ = rotationQ*savedQ;

    // redraw

    update();
}

void magPlot::setPoints(QList<QVector3D> pts)
{
    points = pts;
    update();
}

void magPlot::addGravityPoint(QVector3D p)
{
    gravityPoints.append(p);
    // Every gravity-driven view, not just the cloud: the cube is turned by
    // this too, and testing for one source meant it never moved.
    if (source_ != Source::MagneticField) {
        savedQ = QQuaternion::rotationTo(p, QVector3D(0, 0, -1));
        focusQ = rotationQ * savedQ;
        update();
    }
}

void magPlot::addPoint(QVector3D p)
{
    points.append(p);
    if (source_ != Source::MagneticField) {
        // Still collected, just not on screen.
        return;
    }

    // camera vector along z axis

    QVector3D camera = QVector3D(0,0,-1);

    // compute rotation quaternion

    savedQ = QQuaternion::rotationTo(p,camera);

    // update total rotation

    focusQ = rotationQ*savedQ;

    // redraw

    update();
}

void magPlot::paintEvent(QPaintEvent *event)
{


    QPainter painter(this);

    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform,true);

 

    // fill background

    painter.fillRect(rect(), Qt::white);

    // translate zero point to center

    painter.translate(width()/2.0,height()/2.0);

    // set initial scaling

    const float radius = activeRadius();
    float scale = zoom*height()/(radius*2.5);
    painter.scale(scale,scale);

    if (source_ == Source::Poses) {
        painter.save();
        drawPoseCube(&painter, radius);
        painter.restore();

        painter.save();
        painter.resetTransform();
        painter.setPen(QPen(Qt::darkGray));
        painter.drawText(rect().adjusted(8, 6, -8, 0),
                         Qt::AlignTop | Qt::AlignLeft | Qt::TextWordWrap,
                         caption_);
        painter.restore();
        QWidget::paintEvent(event);
        return;
    }

    // Apply rotation to saved points

    QList<QVector3D> rotated_points;
    for (QVector3D point : activePoints()){
        QVector3D pt = focusQ.rotatedVector(point);
        pt.setY(-pt.y());
        rotated_points.append(pt);
    }

    // Draw background points [ z >= 0]

    painter.save();

    // highlight last point in list

    if (!rotated_points.isEmpty() && rotated_points.last().z() >= 0) // display location of last point added
        drawPoint(&painter,rotated_points.last(),Qt::yellow,highlightFraction*radius);

    // draw points

    for (QVector3D point : rotated_points){
        if (point.z() >= 0) {
            drawPoint(&painter, point, Qt::darkMagenta, dataPointFraction*radius);
        }
    }
    painter.restore();

    // Compute Axes

    QVector3D x1(1.0,0.0,0.0);
    QVector3D y1(0.0,1.0,0.0);
    QVector3D z1(0.0,0.0,1.0);

    x1 = focusQ.rotatedVector(x1);
    y1 = focusQ.rotatedVector(y1);
    z1 = focusQ.rotatedVector(z1);

    QString xlabel = QString("x");
    QString ylabel = QString("y");
    QString zlabel = QString("z");

    // Draw Axes in background (positive z)

    if (x1.z() >= 0)
        drawAxis(&painter,x1,Qt::red,xlabel);
    if (y1.z() >= 0)
        drawAxis(&painter,y1,Qt::green,ylabel);
    if (z1.z() >= 0)
        drawAxis(&painter,z1,Qt::blue,zlabel);

    // draw sphere center

    QPointF center(0.0,0.0);
    painter.save();

    painter.setPen(Qt::NoPen);
    QRadialGradient radGrad(QPointF(0.0,0.0),1.5);
    radGrad.setColorAt(1.0,Qt::darkCyan);
    radGrad.setColorAt(0.0,Qt::cyan);
    painter.setBrush(QBrush(radGrad));
    painter.drawEllipse(center,centerFraction*radius,centerFraction*radius);
    painter.restore();

    // draw field circle -- Use gradient fill based on very light gray

    painter.save();
    painter.setPen(QPen(Qt::gray, strokeFraction*radius));

    // gradient

    radGrad.setCenterRadius(radius);
    radGrad.setColorAt(1.0,QColor(240,240,240,60));
    radGrad.setColorAt(0.0,QColor(240,240,240,180));
    painter.setBrush(QBrush(radGrad));

    // boundary ellipse

    painter.drawEllipse(center,radius,radius);
    painter.restore();

    // draw the axes in forground -- z axis points down!

    if (x1.z() < 0)
        drawAxis(&painter,x1,Qt::red,xlabel);
    if (y1.z() < 0)
        drawAxis(&painter,y1,Qt::green,ylabel);
    if (z1.z() < 0)
        drawAxis(&painter,z1,Qt::blue,zlabel);


    // Draw foreground points (negative z)

    painter.save();

    // highlight last point

    if (!rotated_points.isEmpty() && rotated_points.last().z() < 0) // display location of last point added
        drawPoint(&painter,rotated_points.last(),Qt::yellow,highlightFraction*radius);

    // draw points
    for (QVector3D point :  rotated_points){
        if (point.z() < 0) {
            drawPoint(&painter,point, Qt::darkMagenta, dataPointFraction*radius);
        }
    }
    painter.restore();

    // Name the cloud. The two look alike -- a shell of points around a
    // sphere -- and which one is on screen decides what the operator should
    // do next, so it cannot be left to be inferred.
    painter.save();
    painter.resetTransform();
    QFont label = painter.font();
    label.setPointSizeF(label.pointSizeF() * 1.1);
    painter.setFont(label);
    painter.setPen(QPen(Qt::darkGray));
    painter.drawText(rect().adjusted(8, 6, -8, 0),
                     Qt::AlignTop | Qt::AlignLeft | Qt::TextWordWrap, caption_);
    painter.restore();

    QWidget::paintEvent(event);  // call parent
}

   // draw a point with given color and size -- size
   // decreases with distance

void magPlot::drawPoint(QPainter *p, QVector3D pt, QColor color, float size)
{
    p->setPen(Qt::NoPen);

    if (pt.length() == 0.0)
        return;

    float size_scale = (4.0 - pt.z()/pt.length())/4.0;
    size = size * size_scale;//size - pt.z()/(pt.length()*size*4.0);
    p->setBrush(QBrush(color,Qt::SolidPattern));
    p->drawEllipse(QPointF(pt.x(),-pt.y()),size/2,size/2);
}

  // draw a single axis with given color and end point (pt)

void magPlot::drawAxis(QPainter *p, QVector3D pt, QColor color, QString &text){
    const float radius = activeRadius();
    QPointF pt2 = pt.toPointF();//(pt.x(),pt.y());
    pt.setY(-pt.y());
    p->save();

    // draw axis

    QLineF line = QLineF(pt2*radius, pt2*centerFraction*radius);
    drawPoint(p,pt*radius,color,axisPointFraction*radius);
    p->setPen(QPen(color, strokeFraction*radius));
    p->drawLine(line);

    // label
    /*
    QFont font = p->font();
    font.setPixelSize(axisFontFraction*radius);
    p->setFont(font);
    p->translate(pt2*radius*1.05);
    QFontMetrics fm(p->font());
    QRect textRect = fm.boundingRect(text);
    p->translate(textRect.center());
    p->drawText(0.0,0.0,text);
    */

    p->restore();

}

// Mouse controls

void magPlot::wheelEvent(QWheelEvent *event)
{
    // Get the vertical scroll amount (positive for scrolling up, negative for down)
    int delta = event->angleDelta().y();
    if (delta > 0.0){
        zoom = zoom*1.15;
    }
    if (delta < 0.0){
        zoom = zoom/1.15;
    }
    update();
    QWidget::wheelEvent(event);
}

void magPlot::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::RightButton) {
        m_lastPos = event->position(); // Store the initial position
        m_isDragging = true; // Set a flag to indicate dragging
        old_rotationQ = rotationQ;
    }
    QWidget::mousePressEvent(event);
}

void magPlot::mouseMoveEvent(QMouseEvent *event)
{
    if (m_isDragging && (event->buttons() & Qt::RightButton)) {
        QPointF delta = event->position() - m_lastPos;
        float roll_change = qBound(-90.0,90.0*delta.x()/width(),90.0);
        float pitch_change = qBound(-90.0,-90.0*delta.y()/height(),90.0);
        QQuaternion tmpQ(QQuaternion::fromEulerAngles(-pitch_change, -roll_change,0.0));
        //ENU_NED(tmpQ);
        //tmpQ = QQuaternion(tmpQ.scalar(), tmpQ.y(), tmpQ.x(), -tmpQ.z());
        setFocusQuaternion(tmpQ*old_rotationQ);
    }
    QWidget::mouseMoveEvent(event);
}


void magPlot::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::RightButton) {
        m_isDragging = false; // Reset the dragging flag
    }
    QWidget::mouseReleaseEvent(event); // Call base class implementation
}

