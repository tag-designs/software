1. Initialization and Arming for ActivityThis handles your baseline settings, sets up referenced thresholds, activates Wake-up Mode (6 Hz), and arms the microcontroller to only wake up when motion starts.c

```c
void adxl367_low_power_init(void) {
    // 1. Force Standby to safely modify registers
    adxl367_write_reg(REG_POWER_CTL, 0x00);

    // 2. Configure Thresholds (example: ~250mg act, ~150mg inact)
    adxl367_write_reg(REG_THRESH_ACT_H, 0x00);
    adxl367_write_reg(REG_THRESH_ACT_L, 0xFA); // 250 mg (assuming 1mg/LSB)
    adxl367_write_reg(REG_TIME_ACT, 0x01);     // Must persist 1 sample

    adxl367_write_reg(REG_THRESH_INACT_H, 0x00);
    adxl367_write_reg(REG_THRESH_INACT_L, 0x96); // 150 mg
    adxl367_write_reg(REG_TIME_INACT_H, 0x00);
    adxl367_write_reg(REG_TIME_INACT_L, 0x1E);   // 30 samples (~5 seconds at 6Hz)

    // 3. Configure Activity & Inactivity Control
    // Bits: [ACT_REF, ACT_EN, INACT_REF, INACT_EN] 
    // Link/Loop bits are left 0 to keep the logic in Default Mode
    adxl367_write_reg(REG_ACT_INACT_CTL, 0x3F); 

    // 4. Flush any leftover boot state or pending events
    uint8_t dummy_status;
    adxl367_read_reg(REG_STATUS, &dummy_status);

    // 5. Initial Interrupt State: Map ONLY Activity to INT1
    adxl367_write_reg(REG_INTMAP1, ACT_BIT_MASK);

    // 6. Put hardware into manual Wake-up Mode (Bit 3 = 1)
    // Device now monitors at 6 Hz drawing roughly ~180 nA
    adxl367_write_reg(REG_POWER_CTL, 0x08); 
    
    // 7. Enable your microcontroller's external pin interrupt handler
    mcu_enable_int1_pin_wakeup();
}
```


2. Main Microcontroller ISR (Interrupt Service Routine)When the physical INT1 pin changes state, the microcontroller wakes up and jumps into this centralized handler to figure out whether the device just started moving or just stopped moving.

```c
void mcu_int1_pin_isr_handler(void) {
    uint8_t status_reg = 0;

    // Acknowledge/clear the accelerometer flag by reading STATUS register
    adxl367_read_reg(REG_STATUS, &status_reg);

    // Case A: The device was stationary, and has just STARTED moving
    if (status_reg & ACT_BIT_MASK) {
        handle_activity_start_event();
    }
    
    // Case B: The device was active, and has just STOPPED moving
    else if (status_reg & INACT_BIT_MASK) {
        handle_inactivity_stop_event();
    }
}
```

3. Transition Handlers (The Software "Link Mode")These two helper routines modify the INTMAP1 register dynamically. This approach stops back-to-back re-triggering and effectively tracks your structural state transitions.

```c
void handle_activity_start_event(void) {
    // 1. Turn on system peripherals or perform motion execution tasks
    turn_on_mcu_application_peripherals();

    // 2. Pivot mapping: Stop watching Activity, start watching Inactivity
    adxl367_write_reg(REG_INTMAP1, INACT_BIT_MASK);

    // 3. Clear any edge noise and prepare the microcontroller to sleep
    uint8_t flush;
    adxl367_read_reg(REG_STATUS, &flush);
    
    // MCU can execute code or drop to sleep; continuous shaking won't trip INT1
}

void handle_inactivity_stop_event(void) {
    // 1. Shut down heavy power draw components since device is stationary
    put_application_peripherals_to_sleep();

    // 2. Pivot mapping: Stop watching Inactivity, start watching Activity
    adxl367_write_reg(REG_INTMAP1, ACT_BIT_MASK);

    // 3. Clear the status register to arm the hardware for next motion
    uint8_t flush;
    adxl367_read_reg(REG_STATUS, &flush);
    
    // MCU safely enters deep sleep mode; ADXL367 remains tracking at 180 nA
    mcu_enter_deep_sleep();
}
```


Key Execution Rules Implemented Here
- Never Writes to POWER_CTL Again: Notice that the state machine changes nothing about power state configuration after initialization. The chip stays locked in its 6 Hz frame rate.Double Flush Pattern: 
- Before concluding a state shift, a dummy read to REG_STATUS clears the latching mechanism to verify that an immediate back-to-back edge transition cannot trigger a false interrupt lock.

If you notice any timing bugs during testing, let me know:What is the SPI or I2C clock frequency your application uses?Does your MCU use edge-triggered or level-triggered GPIO interrupts?I can help modify the flush delays if you run into race conditions.