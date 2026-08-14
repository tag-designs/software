# Power Estimation for the IMUTagNand

The following are datasheet estimates of power consumption. Core voltage is
1.8 V, CPU clock 12.5 MHz, peripheral clock 25 MHz.

## Idle Power (Processor in Standby)

| Component | Current (uA) | Notes |
| --- | ---: | --- |
| TPS7A0218PDBVR | 0.025 | |
| RV3028-C7 | 0.045 | |
| STM32U375 | 0.25 | |
| BMM350 | 1.8 | |
| LSM6DSV16X | 2.6 | |
| LPS22HH | 0.9 | |
| GD5F1GQ5REYFGR | 50.0 | |
| **Total** | **55.8** | |

## Run Power in Stop1 (100 Hz Sample Rate)

This estimate is for the periods when there is no activity. BMM350 low-noise -
12.5 Hz ODR, LPS22HH - 10 Hz ODR.

| Component | Current (uA) | Notes |
| --- | ---: | --- |
| TPS7A0218PDBVR | 0.025 | |
| RV3028-C7 | 0.045 | |
| STM32U375 | 150.000 | |
| BMM350 | 57.000 | Low noise |
| LSM6DSV16X | 650.000 | 670 uA in high accuracy |
| LPS22HH | 107.000 | Low noise |
| GD5F1GQ5REYFGR | 50.000 | |
| **Total** | **1014** | |

## Run Power in Stop1 (200 Hz Sample Rate)

This estimate is for the periods when there is no activity. BMM350 low-noise -
25 Hz ODR, LPS22HH - 25 Hz ODR.

| Component | Current (uA) | Notes |
| --- | ---: | --- |
| TPS7A0218PDBVR | 0.025 | |
| RV3028-C7 | 0.045 | |
| STM32U375 | 150.000 | |
| BMM350 | 96.000 | Low noise |
| LSM6DSV16X | 650.000 | 670 uA in high accuracy |
| LPS22HH | 265.000 | Low noise |
| GD5F1GQ5REYFGR | 50.000 | |
| **Total** | **1211** | |

## Run Power in Stop1 (400 Hz Sample Rate)

This estimate is for the periods when there is no activity. BMM350 low-noise -
50 Hz ODR, LPS22HH - 50 Hz ODR.

| Component | Current (uA) | Notes |
| --- | ---: | --- |
| TPS7A0218PDBVR | 0.025 | |
| RV3028-C7 | 0.045 | |
| STM32U375 | 150.000 | |
| BMM350 | 175.000 | Low noise |
| LSM6DSV16X | 650.000 | 670 uA in high accuracy |
| LPS22HH | 530.000 | Low noise |
| GD5F1GQ5REYFGR | 50.000 | |
| **Total** | **1555** | |

## Run Power in Stop1 (800 Hz Sample Rate)

This estimate is for the periods when there is no activity. BMM350 low-noise -
100 Hz ODR, LPS22HH - 100 Hz ODR.

| Component | Current (uA) | Notes |
| --- | ---: | --- |
| TPS7A0218PDBVR | 0.025 | |
| RV3028-C7 | 0.045 | |
| STM32U375 | 150.000 | |
| BMM350 | 335.000 | Low noise |
| LSM6DSV16X | 650.000 | 670 uA in high accuracy |
| LPS22HH | 338.000 | Low current |
| GD5F1GQ5REYFGR | 50.000 | |
| **Total** | **1523** | |

## Run Power in Stop1 (1600 Hz Sample Rate)

This estimate is for the periods when there is no activity. BMM350 regular -
200 Hz ODR, LPS22HH - 200 Hz ODR.

| Component | Current (uA) | Notes |
| --- | ---: | --- |
| TPS7A0218PDBVR | 0.025 | |
| RV3028-C7 | 0.045 | |
| STM32U375 | 150.000 | |
| BMM350 | 370.000 | Regular |
| LSM6DSV16X | 650.000 | 670 uA in high accuracy |
| LPS22HH | 482.000 | Low current |
| GD5F1GQ5REYFGR | 50.000 | |
| **Total** | **1702** | |

This does not include the cost of reading data or writing flash. This is just
the idle periods.

## Actual Measurements

Note: these measurements used software I2C, which averages 2.4 mA for 1.9 ms.

| Mode | Estimate (uA) | Measured (uA) | Notes |
| --- | ---: | ---: | --- |
| Idle | 55 | | Standby does not work; getting 20 uA in Stop3 |
| 100 Hz | 1014 | 1520 | |
| 200 Hz | 1211 | 1600 | |
| 400 Hz | 1555 | 1800 | |
| 800 Hz | 1523 | 2259 | BMM350 switched to low current |
| 1600 Hz | 1702 | 2630 | |

The estimates did not include communication costs with I/O devices and memory.
Updated to use hardware I2C.

Page write is 11 uJ. So memory write is 1.4 J for 128k pages.

## Storage-Limited Runtime

Each flash page contains 150 IMU samples. These estimates assume the external
memory is filled with contiguous 2048-byte log pages and use raw memory
capacity: 1 Gbit is 65,536 pages and 2 Gbit is 131,072 pages. Actual runtimes
will be slightly lower after bad blocks and metadata/checkpoint overhead.

| Sample Rate | Page Rate | 1 Gbit Runtime | 2 Gbit Runtime |
| ---: | ---: | ---: | ---: |
| 100 Hz | 0.667 pages/s | 27.3 h (1.14 d) | 54.6 h (2.28 d) |
| 200 Hz | 1.333 pages/s | 13.7 h (0.57 d) | 27.3 h (1.14 d) |
| 400 Hz | 2.667 pages/s | 6.83 h (0.28 d) | 13.7 h (0.57 d) |
| 800 Hz | 5.333 pages/s | 3.41 h (0.14 d) | 6.83 h (0.28 d) |
| 1600 Hz | 10.667 pages/s | 1.71 h (0.07 d) | 3.41 h (0.14 d) |

## After Fixing Stop1 Code

| Mode | Estimate (uA) | Measured (uA) | Notes |
| --- | ---: | ---: | --- |
| Idle | 55 | 14.2 | |
| 100 Hz | 1014 | 920 | |
| 200 Hz | 1211 | 998 | |
| 400 Hz | 1555 | 1260 | |
| 800 Hz | 1523 | 1700 | BMM350 switched to low current |
| 1600 Hz | 1702 | 2300 | |
