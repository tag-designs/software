#ifndef TXTDOWNLOAD_H
#define TXTDOWNLOAD_H

#include "abstractdownload.h"
#include <fstream>
#include <taglogs.h>

class TxtDownload: public AbstractDownload
{
    Q_OBJECT

    public: 
        TxtDownload(Tag &t, QString &p, QObject *parent = 0);
        ~TxtDownload();
        std::fstream fs;

    private:
        bool dumpHeader(void) override; 
        int dumpLog(Ack &ack) override;
       
};

#endif