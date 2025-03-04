#include <QTime>
#include <QTimer>
#include <QFile>
#include <QDataStream>
#include <QDebug>

#include "flashdownload.h"

#define RAWDATASIZE 123
typedef struct {
  // header = 8 bytes
  uint32_t start;
  uint32_t end;
  uint8_t start_subsec;
  uint8_t end_subsec;
  uint16_t data[RAWDATASIZE];  // 41 samples * (3 * 2)
} rawdata;

FlashDownload::FlashDownload(QObject *parent) : QObject(parent){  }

void FlashDownload::start(Tag *t, QTextStream *_stream,
              unsigned int _firstpage, unsigned int _pages, bool _writeCSV)
{
    // set the range on progress bar

    emit progressRangeChanged(0, _pages);
    emit progressValueChanged(0);

    // create private copies of input arguments

    firstpage = _firstpage;
    pages = _pages;
    cnt   = 0;
    stream = _stream;
    writeCSV = _writeCSV;
    tag = t;

    // trigger our download worker

    if (pages > 0 && stream)
        QTimer::singleShot(0, this, SLOT(download()));
}

// download worker

void FlashDownload::download()
{
    QTime t;
    t.start();
     const QString dtype[] = {"x ", "y ", "z ", "t "};

    // limit worker to 150 ms work quantum

    while (t.elapsed() < 150) {
          int offset = cnt * 2048;
          int len = 0;
          int read = 0;
          uint8_t buffer[2048];
          do {
            std::string tmp;
            bool retval = tag->GetBlkData(tmp, 0, offset,  2048 - read);
            qDebug() << "read data " << retval;
            if (!retval) break;
            qDebug() << "data len " << tmp.length();
            len = static_cast<int>(tmp.length());
            bcopy(tmp.c_str(), &buffer[read], len);
            offset += len;
            read += len;
          }  while (len && read < 2048);


          if (read == 2048) {
              *stream << "# page " << QString::number(cnt) << "\n";

                 for (int i = 0; i < 8; i++) {
                  rawdata *buf = (rawdata *) &buffer[i*sizeof(rawdata)];
                  *stream << "\n# Start "
                         << QString::number(buf->start + (buf->start_subsec&0x7f) / 100.0, 'f', 3)
                         << "\n";
                  *stream << "# End "
                         << QString::number(buf->end + (buf->end_subsec&0x7f) / 100.0, 'f', 3) << "\n";
                  *stream << "# Active " << QString::number(buf->end_subsec >> 7) << "\n";
                  for (int j = 0; j < 123; j += 1) {
                     *stream <<  dtype[(buf->data[j]>>14) & 3];
                     int16_t val = (buf->data[j] << 2);
                     val = val >> 2;
                     *stream  << val;
                    if (j % 3 == 2)
                      *stream << "\n";
                     else {
                      *stream << " ";
                    }
                  }
              }
            }  else {
                qDebug() << "Buf size too small " << read;
            }
          cnt++;
          // check for completion

          if (cnt == pages) {
            emit finished();
            return;
          }
    }

    // not finished -- update progress bar

    emit progressValueChanged(cnt);

    // yield to the UI loop and schedule next work quantum

    QTimer::singleShot(0, this, SLOT(download()));
}
