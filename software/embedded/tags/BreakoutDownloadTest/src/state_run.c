#include "hal.h"
#include <limits.h>
#include "app.h"

#include "tag.pb.h"
#include "config.h"
#include "persistent.h"
#include "datalog.h"
#include "lps.h"

enum Sleep Running(enum StateTrans t, State_Event reason)
{
  int16_t temp10;
  uint16_t vdd100;
  if (t == T_ERROR)
  { 
    // recovery code for brownout here?
    return Aborted(T_INIT, reason);
  }

  if (t == T_INIT)
  {
    // initialize the persistent state
    pState->state = TagState_RUNNING;
    recordState(reason);
    // make sure we're pointing to the next data block in case
    // this is a recovery action -- round up in the case of partial blocks
    // written

    // Start the interval timer
    disableAllAlarms();
    disableTicker();
    enableTicker(sconfig.lps_period > 0 ? sconfig.lps_period : 1);
  }
  else
  {
    t_DataHeader dataheader;
    struct{
      int16_t pressure;
      int16_t temperature;
    } datablock[60];

    int16_t pressure,temperature;

    // Create some fake data

    if (events & EVT_RTC_WUTF)
    {
      dataheader.epoch = timestamp+(pState->pages)*sconfig.lps_period*60;
      dataheader.vdd100[0] = 2000 + pState->pages*10;
      dataheader.vdd100[1] = 3000 + pState->pages*10;
      writeDataHeader(&dataheader);
      //lpsGetPressureTemp(&pressure, &temperature);
      for (int j = 0; j < 60; j++){ 
        datablock[j].pressure = 1000+j*100;
        datablock[j].temperature = 500+j*100;
      }  
      writeDataLog((uint16_t *)&datablock[0], 240);
      pState->external_blocks += 60;
      
      if (pState->pages == 200)
        return Finished(T_INIT, State_EVENT_ENDTIM);
    }
  }
  return STOP2;
   
}
