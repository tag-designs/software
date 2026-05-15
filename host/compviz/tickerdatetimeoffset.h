#ifndef TICKERDATETIMEOFFSET_H
#define TICKERDATETIMEOFFSET_H

#include "qcustomplot.h"

// Subclass QCPAxisTickerDateTime to label based upon offset
// from UTC

class TickerDateTimeOffset : public QCPAxisTickerDateTime
{
public:
    void setOffset(double off);
    TickerDateTimeOffset();

protected:
 virtual QString getTickLabel(double tick, const QLocale &locale,
                              QChar formatChar, int precision);
private:
    double offset;
};

#endif // TICKERDATETIMEOFFSET_H
