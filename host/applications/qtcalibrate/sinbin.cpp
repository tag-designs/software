// Integer signusoidal bins using algorithm from
// https://oceancolor.gsfc.nasa.gov/resources/docs/format/l3bins/

#include "sinbin.h"

static float constrain_lon(float lon){
    while (lon < -180) 
        lon += 360;
    while (lon > 180)
        lon -= 360;
    return lon;
}

static float constrain_lat(float lat){
    if (lat > 90)
        lat = 90;
    if (lat < -90)
        lat = -90;
    return lat;
}

SinBin::SinBin(int rows) {
    numrows = rows;
    numbin = new int[numrows];
    basebin = new int[numrows];
    latbin = new float[numrows];
    
    int row;

    basebin[0] = 1;
    for (row = 0; row < numrows; row++){
        latbin[row] = ((row + 0.5)*180/numrows) - 90.0;
        numbin[row] = int(2*numrows*cos(latbin[row]*M_PI/180.0) +0.5);
        if (row) {
            basebin[row] = basebin[row-1] + numbin[row-1];
        }
    }
    totbins = basebin[numrows-1] + numbin[numrows-1] - 1;
    xbin = new float[totbins];
    ybin = new float[totbins];
    zbin = new float[totbins];

    for(int bin = 0; bin < totbins; bin++){
        float lat, lon;
        bin2latlon(bin, lat, lon);
        xbin[bin] = cos(lat*M_PI/180.0) * cos(lon*M_PI/180.0);
        ybin[bin] = cos(lat*M_PI/180.0) * sin(lon*M_PI/180.0);
        zbin[bin] = sin(lat*M_PI/180.0);
    }
}

SinBin::~SinBin(){
    delete numbin;
    delete basebin;
    delete latbin;
    delete xbin;
    delete ybin;
    delete zbin;
}

int SinBin::lat2row(float lat){
    int row = int((90 + lat)*numrows/180.0);
    if (row >= numrows)
        row = numrows-1;
    return row;
}

int SinBin::rowlon2bin(int row, float lon){
    lon = constrain_lon(lon);

    int col = int((lon + 180.0)*numbin[row]/360.0);
    if(col >= numbin[row])
    {
        col = numbin[row] - 1;
    }
    return basebin[row] + col;
}

int SinBin::latlon2bin(float lat, float lon) {

    lat = constrain_lat(lat);
    lon = constrain_lon(lon);

    int row = lat2row(lat);
    int col = int((lon + 180.0)*numbin[row]/360.0);
    if(col >= numbin[row])
    {
        col = numbin[row] - 1;
    }
    return basebin[row] + col;
}

void SinBin::bin2latlon(int bin, float& lat, float& lon){
    int row = numrows - 1;
    if (bin < 1)
    {
        bin = 1;
    }
    while(bin < basebin[row]){
        row--;
    }
    lat = latbin[row];
    lon = 360.0*(bin - basebin[row] + 0.5)/numbin[row] - 180.0;
}

bool SinBin::bin2point(int bin, float& x, float& y, float& z){
    if ((bin > totbins) || (bin < 0))
        return false;
    x = xbin[bin];
    y = ybin[bin];
    z = zbin[bin];
    return true;
}