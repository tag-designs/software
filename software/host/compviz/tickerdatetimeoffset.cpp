
#include "qcustomplot.h"
#include "tickerdatetimeoffset.h"

// needed to subclass TickerDateTime

TickerDateTimeOffset::TickerDateTimeOffset()
{
    offset = 0.0;

}

QString TickerDateTimeOffset::getTickLabel(double tick, const QLocale &locale,
                               QChar formatChar, int precision) {
    return QCPAxisTickerDateTime::getTickLabel(tick + offset, locale, formatChar, precision);
}

void TickerDateTimeOffset::setOffset(double off){
    offset = off;
}

