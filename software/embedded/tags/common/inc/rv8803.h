#ifndef RV8803
#define RV8803

#define RV8803_ADR (0x32)

enum RV8803Reg {
  RV8803_SEC,                // 0x00
  RV8803_MIN,                //	0x01
  RV8803_HOUR,               //	0x02
  RV8803_WDAY,               // 0x03
  RV8803_DAY,                //	0x04
  RV8803_MONTH,              // 0x05
  RV8803_YEAR,               //	0x06
  RV8803_RAM,                // 0x07
  RV8803_ALARM_MIN,          //	0x08
  RV8803_ALARM_HOUR,         //	0x09
  RV8803_ALARM_WEEK_OR_DAY,  //	0x0A
  RV8803_TIMER_COUNT0,       // 0x0B
  RV8803_TIMER_COUNT1,       // 0x0C
  RV8803_EXT,                //	0x0D
  RV8803_FLAG,               //	0x0E
  RV8803_CTRL,               // 0x0F
  RV8803_HUNDREDTHS,         // 0x10
  RV8803_SECONDS_SHADOW,     // 0x11
};

#define RV8803_EXT_WADA BIT(6)

#define RV8803_FLAG_V1F BIT(0)
#define RV8803_FLAG_V2F BIT(1)
#define RV8803_FLAG_AF BIT(3)
#define RV8803_FLAG_TF BIT(4)
#define RV8803_FLAG_UF BIT(5)

#define RV8803_CTRL_RESET BIT(0)

#define RV8803_CTRL_EIE BIT(2)
#define RV8803_CTRL_AIE BIT(3)
#define RV8803_CTRL_TIE BIT(4)
#define RV8803_CTRL_UIE BIT(5)

#define RX8900_BACKUP_CTRL 0x18
#define RX8900_FLAG_SWOFF BIT(2)
#define RX8900_FLAG_VDETOFF BIT(3)

typedef struct {
  uint8_t seconds;
  uint8_t minutes;
  uint8_t hours;
  uint8_t weekday;  // we ignore this !
  uint8_t date;
  uint8_t month;
  uint8_t year;
} RV8803_DateTime;

#endif
