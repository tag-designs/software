#include <math.h>
#include <QVector3D>

// integer sinusoidal bins -- approximately equal sized.
// See: Integerized Sinusoidal Binning Scheme for Level 3 Data
// https://oceancolor.gsfc.nasa.gov/resources/docs/format/l3bins/


class SinBin {
    public:
        SinBin(int rows);
        ~SinBin();
        int lat2row(float);
        int rowlon2bin(int row, float lon);
        int latlon2bin(float lat,float lon);
        void bin2latlon(int bin, float& lat, float& lon);
        bool bin2point(int bin, float& x, float& y, float& z);
        int bins() { return totbins;};
        int rows() { return numrows;};


    private:
        int totbins;
        int numrows;
        int *numbin;
        int *basebin;
        float *latbin;
        float *xbin;
        float *ybin;
        float *zbin;
};