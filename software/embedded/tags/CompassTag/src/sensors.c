#include "hal.h"
#include "monitor.h"
#include "app.h"
#include "lis2du12.h"
#include "ak09940a.h"
#include <tag.pb.h>

#include <stdint.h>
#include <strings.h>

void magOn(void);
void magOff(void);



bool sensorSample(SensorData *sensors)
{
    uint8_t buf[11];
    const int samples = 1;
    struct {
        int16_t x;
        int16_t y;
        int16_t z;
    } accel_data;

    int x = 0;
    int y = 0;
    int z = 0;
    int t = 0;
   
    sensors->has_mag = true;
    for (int i=0;i<samples;i++) {
        while (!magSample(buf));
        x += ((int) (buf[2]<<30)|(buf[1]<<22) | (buf[0] << 14))>>14;
        y += ((int) (buf[5]<<30)|(buf[4]<<22) | (buf[3] << 14))>>14;
        z += ((int) (buf[8]<<30)|(buf[7]<<22) | (buf[6] << 14))>>14;
        t += ((int)(buf[9]));
    }
    x = ((x/samples) + 2);
    y = ((y/samples) + 2);
    z = ((z/samples) + 2);
    t = t/samples;

    sensors->mag.mx = x/100.0f;
    sensors->mag.my = y/100.0f;
    sensors->mag.mz = z/100.0f;
    sensors->mag.temperature = 30.0f - t/1.7f;

    if (accelSample((uint8_t *) &accel_data))
    {
        sensors->has_accel = true;
        sensors->accel.ax = (accel_data.x/16)*0.976f;
        sensors->accel.ay = (accel_data.y/16)*0.976f;
        sensors->accel.az = (accel_data.z/16)*0.976f;

    }

    return true;
}
bool initSensors(void){
    accelInit();

    //magOn();
    //stopMilliseconds(true,3);
    return true;
}
bool deinitSensors(void) {
    //magOff();
    accelDeinit();
    return true;
}



