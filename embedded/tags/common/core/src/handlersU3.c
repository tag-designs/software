/**
 * @file handlersU3.c
 * @brief STM32U3 debug-monitor interrupt implementation.
 *
 * This file is included by handlers.c after the shared monitor state and
 * helpers have been declared.
 */

CH_IRQ_HANDLER(DebugMon_Handler) {
  CH_IRQ_PROLOGUE();

  uint32_t input = (CoreDebug->DCRDR);
  uint8_t operation = (input & 0xff);
  int operand = (input >> 8);

  // could use operand for packet length

  if (monitorRequestPending()) {
    switch (operation) {
      case TAG_MONITORINFO:
        switch (operand) {
          case (MONITORVERSION):
            CoreDebug->DCRDR = (uint32_t)DEBUGVERSION;
            break;
          case (MONITORBUF):
            CoreDebug->DCRDR = (uint32_t)ProtoBuf;
            break;
          case (MONITORBUFSIZE):
            CoreDebug->DCRDR = (uint32_t)sizeof(ProtoBuf);
            break;
          case (TAGSHASTR):
            CoreDebug->DCRDR = (uint32_t) SHAStr;
            break;
        }
        monitorClearRequest();
        break;
      case MONITORSTART:
        chSysLockFromISR();
        palSetLine(LINE_testpin);
        monitor_enabled = true;
        monitor_pending = false;
        monitor_timeout_pending = false;
        monitorArmTimeoutI();
#if defined(RANGE_MULTIPLIER) && RANGE_MULTIPLIER
        if (!monitor_clock_fast)
        {
          fast_msi();
          monitor_clock_fast = true;
        }
#endif
        CoreDebug->DCRDR = 1;
        monitorClearRequest();
        if (tpMain)
          chEvtSignalI(tpMain, EVT_MONITOR_SERVICE);
        chSysUnlockFromISR();
        break;
      case MONITORSTOP:
        chSysLockFromISR();
        //palClearLine(LINE_testpin);
        CoreDebug->DCRDR = 1U;
        monitorStopI(false);
        chSysUnlockFromISR();
        break;
      case PROTOBUF:  // execute later in main-thread context
        chSysLockFromISR();
        if (monitor_enabled && !monitor_pending) {
          monitor_operand = operand;
          monitor_pending = true;
          CoreDebug->DCRDR = 0;
          monitorArmTimeoutI();
          if (tpMain)
            chEvtSignalI(tpMain, EVT_MONITOR_SERVICE);
        } else {
          monitorClearRequest();
        }
        chSysUnlockFromISR();
        break;
      default:
        monitorClearRequest();
        break;
    }
  }
  __DSB();
  CH_IRQ_EPILOGUE();
}
