#ifndef ACTOGRAM_H
#define ACTOGRAM_H

#include <QWidget>
#include <QDateTime>
#include <qcustomplot.h>
#include <tickerdatetimeoffset.h>

namespace Ui {
class Actogram;
}

class Actogram : public QWidget
{
    Q_OBJECT

public:
    explicit Actogram(QWidget *parent = 0);
    ~Actogram();
    void setData(QVector<double> time, QVector<double> value, QString fname);

private slots:

    // Configuration

    void on_startDay_dateChanged(const QDate &date);
    void on_sb_actorange_valueChanged(const QString &arg1);
    void on_sb_startday_valueChanged(int value);
    void on_days_valueChanged(const QString &arg1);

    // export

    void on_pb_acto_png_clicked();
    void on_pb_acto_pdf_clicked();
    void on_le_actogram_textChanged(const QString &arg1);

    void on_sb_offset_valueChanged(int arg1);

    void on_cb_double_toggled(bool checked);

    // daylight controls

    void on_rb_no_light_clicked();
    void on_rb_natural_light_clicked();

    // location controls

    void location_changed();

    void on_rb_lab_light_clicked();

    void on_pb_load_lightfile_clicked();

private:
    Ui::Actogram *ui;

    // configuration

    const int agg_time = 300;

    // graph data

    QVector<QCPGraph *>actos;
    QVector<QCPAxisRect *>rects;
    QVector<QCPItemText *> labels;
    QSharedPointer<TickerDateTimeOffset> dateTicker;
    QVector<double> tmdata;
    QVector<double> valdata;
    QVector<double> lightdata;
    QVector<double> tmLabLight;
    QVector<double> valLabLight;

    QString LabLightFile;

    // graph time range

    QDateTime startDT;
    QDateTime endDT;

    // metadata

    QCPTextElement *title;
    QCPTextElement *footleft;
    QCPTextElement *footcenter;
    QCPTextElement *footright;
    QString fileName;

    // private functions

    void drawActogram();
    void drawMetadata(int numgraphs);
    void generateNaturalLightData();

};

#endif // ACTOGRAM_H
