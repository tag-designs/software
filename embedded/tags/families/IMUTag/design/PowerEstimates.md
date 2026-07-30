# Power estimation for the IMUTagNand

The following are datasheet estimates of power consumption.  Core voltage is 1.8V, CPU clock 12.5 mHz, peripheral clock 25 mHz

## Idle Power (Processor in Standby)

    - TPS7A0218PDBVR    0.025uA
    - rv3028-C7:        0.045uA
    - stm32U375:        0.25uA
    - bmm350:           1.8uA
    - lsm6dsv16x:       2.6uA
    - lps22hh:          0.9uA
    - GD5F1GQ5REYFGR:  50.0uA
    --------------------------
       Total           55.8uA


## Run Power in Stop1 (100hz Sample Rate)   

This estimate is for the periods when there is no activity.  bmm350 low-noise - 12.5hz ODR, lps22hh - 10hz ODR

    - TPS7A0218PDBVR       0.025uA
    - rv3028-C7:           0.045uA
    - stm32U375:         150.000uA
    - bmm350:             57.000uA (Low Noise)
    - lsm6dsv16x:        650.000uA (670 in high accuracy)
    - lps22hh:           107.000uA (Low Noise)
    - GD5F1GQ5REYFGR:     50.000uA
    --------------------------
       Total             1014 uA       

## Run Power in Stop1 (200hz Sample Rate)   

This estimate is for the periods when there is no activity.  bmm350 low-noise - 25hz ODR, lps22hh - 25hz ODR

    - TPS7A0218PDBVR       0.025uA
    - rv3028-C7:           0.045uA
    - stm32U375:         150.000uA
    - bmm350:             96.000uA (Low Noise)
    - lsm6dsv16x:        650.000uA (670 in high accuracy)
    - lps22hh:           265.000uA (Low Noise)
    - GD5F1GQ5REYFGR:     50.000uA
    --------------------------
       Total           1211uA


## Run Power in Stop1 (400hz Sample Rate)   

This estimate is for the periods when there is no activity.  bmm350 low-noise - 50hz ODR, lps22hh - 50hz ODR

    - TPS7A0218PDBVR       0.025uA
    - rv3028-C7:           0.045uA
    - stm32U375:         150.000uA
    - bmm350:            175.000uA (Low Noise)
    - lsm6dsv16x:        650.000uA (670 in high accuracy)
    - lps22hh:           530.000uA (Low Noise)
    - GD5F1GQ5REYFGR:     50.000uA
    --------------------------
       Total           1555uA

## Run Power in Stop1 (800hz Sample Rate)   

This estimate is for the periods when there is no activity.  bmm350 low-noise - 100hz ODR, lps22hh - 100hz ODR

    - TPS7A0218PDBVR       0.025uA
    - rv3028-C7:           0.045uA
    - stm32U375:         150.000uA
    - bmm350:            335.000uA (Low Noise)
    - lsm6dsv16x:        650.000uA (670 in high accuracy)
    - lps22hh:           338.000uA (Low Current)
    - GD5F1GQ5REYFGR:     50.000uA
    --------------------------
       Total           1523uA

## Run Power in Stop1 (1600hz Sample Rate)   

This estimate is for the periods when there is no activity.  bmm350 low-noise - 200hz ODR, lps22hh - 200hz ODR

    - TPS7A0218PDBVR       0.025uA
    - rv3028-C7:           0.045uA
    - stm32U375:         150.000uA
    - bmm350:            370.000uA (Regular)
    - lsm6dsv16x:        650.000uA (670 in high accuracy)
    - lps22hh:           482.000uA (Low Current)
    - GD5F1GQ5REYFGR:     50.000uA
    --------------------------
       Total           1702uA


This does not include the cost of reading data or writing flash.  This is just the idle periods.

Actual Measurements [Note this is with software i2c which averages 2.4mA for 1.9ms]

Idle:     55uA      211uA
100hz:  1014uA     1520uA
200hz:  1211uA     1600uA
400hz:  1555uA     1840uA
800hz:  1523uA     2330uA  [bmm switch to low current]
1600hz: 1702uA     2800uA 

The estimates did not include communication costs with I/O devices and memory.  Software I2C is especially expensive. [what resistors id breakout board use?]