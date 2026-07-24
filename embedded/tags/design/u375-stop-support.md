## Low power features

- SRAM1 could be powered off and use SRAM2
    - linker changes
    ```c
    /* Change the primary ram0 region to target SRAM2 instead of SRAM1 */
    ram0 (xrw)  : ORIGIN = 0x20030000, LENGTH = 64K  /* SRAM2 Bounds */
    ram1 (xrw)  : ORIGIN = 0x00000000, LENGTH = 0
    ```
    - power off SRAM1
     ```c
     int main(void) {
    /* Initialize ChibiOS HAL and Kernel Core */
    halInit();
    chSysInit();

    /* Ensure no ongoing memory allocations remain, then shut down SRAM1 Accessing the STM32U3 Power Control registers directly */
    chSysLock();
    PWR->CR1 |= PWR_CR1_SRAM1PD; 
    chSysUnlock();

     ```

- Enable stop0 or stop1 in idle
    -  need to move tickless support to lptim
    -  need pre idle hook to enable power for active devices
    -  need idle hook to manage transition to stop, for example in chconf.h
    ```c
    #define CH_CFG_IDLE_LOOP_HOOK() {            \
    osalSysLock();                            \
    /* Enter Stop 1 mode instead of standard WFI Sleep */ \
    SCB->SCR |= SCB_SCR_SLEEPDEEP_Msk;       \
    PWR->CR1 = (PWR->CR1 & ~PWR_CR1_LPMS) | PWR_CR1_LPMS_STOP1; \
    __WFI();                                 \
    osalSysUnlock();                          \
    ```
    -  need post idle hook to unwind power for active devices
    -  need way for active thread to wait on lsm interrupt


## example of main tread waiting on pin interrupt

```c
#include "ch.h"
#include "hal.h"

// 1. Declare the binary semaphore
static binary_semaphore_t pin_bsem;

// 2. Define the callback function that runs inside the ISR context
static void gpio_exti_callback(void *arg) {
  (void)arg;
  
  chSysLockFromISR();
  chBSemSignalI(&pin_bsem); // Wake up the waiting thread
  chSysUnlockFromISR();
}

// 3. Thread that waits for the pin interrupt
static THD_WORKING_AREA(wa_pin_thd, 128);
static THD_FUNCTION(pin_thd, arg) {
  (void)arg;
  while (true) {
    chBSemWait(&pin_bsem);
    // Pin interrupt occurred! Put your processing code here.
  }
}

void main(void) {
  halInit();
  chSysInit();

  // 4. Initialize the semaphore
  chBSemObjectInit(&pin_bsem, false);

  // 5. Configure the pin mode (e.g., GPIOC, Pin 13)
  palSetPadMode(GPIOC, 13, PAL_MODE_INPUT_PULLUP);

  // 6. Attach the callback to the specific pin and choose the trigger
  // Options: PAL_EVENT_MODE_RISING, PAL_EVENT_MODE_FALLING, or PAL_EVENT_MODE_BOTH
  palEnablePadEvent(GPIOC, 13, PAL_EVENT_MODE_FALLING);
  palSetPadCallback(GPIOC, 13, gpio_exti_callback, NULL);

  // 7. Start your worker thread
  chThdCreateStatic(wa_pin_thd, sizeof(wa_pin_thd), NORMALPRIO, pin_thd, NULL);

  while (true) {
    chThdSleepMilliseconds(1000);
  }
}
```

Here's an example with a timeout

```c
static THD_WORKING_AREA(wa_pin_thd, 128);
static THD_FUNCTION(pin_thd, arg) {
  (void)arg;
  
  // Convert your desired time (e.g., 500ms) to ChibiOS system ticks
  sysinterval_t timeout_ticks = TIME_MS2I(500);

  while (true) {
    // Wait for the semaphore with a specified timeout
    msg_t result = chBSemWaitTimeout(&pin_bsem, timeout_ticks);

    if (result == MSG_OK) {
      // SUCCESS: The pin interrupt triggered before the timeout expired!
      // Put your edge processing code here.
      
    } else if (result == MSG_TIMEOUT) {
      // TIMEOUT: 500ms passed with no pin activity.
      // Put your fallback or error handling code here.
      
    }
  }
}
```

## Things that might need to remain powered in stop1

- spix
- i2c
- gpdma
- gpio ?? especially for the lsm interrupt pin

## What if debugger is attached

- need to enable debug in stop mode within the pre idle code, and remove it in the post idle code.

## Attention

- if we use a clock faster than 24mhz, then there can be problems waking from stop1
