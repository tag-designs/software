# Power Measurement
 
Measured 6/14/26 (0d21610)

- idle:  8.2uA 
- 50hz:  766uA
- 100hz: 868uA
- 200hz: 985uA
- 400hz: 1195uA
- 800hz: 1397uA
- 1600hz: 1750uA

There's a problem at idle and at finished -- I don't think the devices are shutdown and it doesn't look like there's a clear path where this would happen.  For example, after collecting data, I'm not sure that lptim2 is shut off and the IMU reset.  I don't know about the pressure sensor or magnetometer either.

Measured 7/10/27  (ae40ae7) after implementing sleep mode for dma spi writes

- idle:  8.7uA 
- 50hz:  778uA
- 100hz: 890uA
- 200hz: 1004uA
- 400hz: 1242uA
- 800hz: 1648uA
- 1600hz: 2291uA

It's not clear why the measurements went up.

Measured 7/16/27 with Joulescope (ae40ae7)

- idle:  8.05uA 
- 50hz:  783uA
- 100hz: 894uA
- 200hz: 1023uA
- 400hz: 1264uA
- 800hz: 1710uA
- 1600hz: 2340uA

## Expected Idle power

 - Lsm6dsv16x -- 2.5 uA
 - ak09940a   -- 1.0 uA
 - lps22hh.   -- 0.9 uA
 - mx25l      -- 10  uA
 - TPS7A02    -- 25  nA
 - RTC and MPU -- 200 nA

 Total should be ~5 uA

 ## Idle fix

 After ensuring that device are shutdown in standby, power is 6.8uA.  Slightly above budget, but ok for this device.


# Measuring Log Download Speed

We instrumented the tag and base in the following way:

1) Probes on External Flash select and clock lines
2) GPIO Pin toggle in log_ack function -- initially around the whole function and later around the ack_encode call
3) GPIO Pin toggle in base around calls to process stlink messages

The following are the intial measurements:

1) Time for full log download cycle: 28.6 ms
2) Time for full log_ack call: 13 ms
3) Time for Flash read: 2.27 ms
4) Time for Stlink to read protobuf containing log mesg: 12.8 ms

Thus the two dominant times were creating the log_ack message (13-2.27 = 11.73ms) and the stlink read (12.8 ms)

## Nanopb guidance from the web:

Streamline Message Structures

- Flatten Submessages: Submessages require separate size calculations, which can take time. Minimizing submessages and structuring flat messages can improve encoding speed by up to 2×.

- Avoid Callbacks for Simple Fields: While dynamic sizes are highly flexible, callbacks require additional function pointer lookups. Sticking to static, fixed-size fields whenever possible reduces runtime overhead.

- Direct Function Calls: Program direct calls to encoding functions instead of looping through generic encoders to increase speed by up to 5×

## Historical Log Format

The first optimized IMUTag download format packed 16 submessages that were
largely, but not exclusively raw bytes. These were generated from 128 byte raw
internal messages.

```protobuf
message IMUTagLogData{
  int32 pressure_raw = 1; // legacy compact pressure; hPa = raw / 16; -1 means no sample
  int32 mx_raw = 2;       // legacy compact mag; uT = raw * 0.04; all axes -1 means no sample
  int32 my_raw = 3;
  int32 mz_raw = 4;
  bytes data = 5; // packed accel, gyro data 16bit integers
                           // gx,gy,gz,ax,ay,az
}

message IMUTagLog{
  int32 epoch = 1;
  int32 millisecond = 2;
  float temperature = 3;
  repeated IMUTagLogData data = 4;
  ```

## Changes

Because the old log message consisted of 16 submessages that were largely
packed bytes, we chose to merge these into a single bounded raw log message
format:

```protobuf
message IMUTagRawLog{
  int32 epoch = 1;
  int32 millisecond = 2;
  float temperature = 3;
  bytes samples = 4; // packed t_DataLog payload images
}
```

Historical note: before the 2048-byte page format, `t_DataLog` blocks contained
eight IMU samples plus full raw pressure,
pressure temperature, and full-scale magnetometer fields. Footer flags mark
which environmental samples are valid and whether a block has been written.
The tag uses those footer flags to omit trailing erased blocks from partially
filled external pages.

### both original and compressed formats assume the decoder respects little-endian data!!

## Revised performance

1) Time for full log download cycle: 17 ms
2) Time for full log_ack call: 3 ms
3) Time for Flash read: 2.27 ms
4) Time for Stlink to read protobuf containing log mesg: 12.8 ms

Now the dominant cost is the swd implementation.  Reformating the log message increased overall performance by almost 2x.

![Timing before optimization](Log%20download%20cycle%20IMUTagBreakout.png)


![Timing after optimization](Log%20download%20cycle%20IMUTagBreakout%20(raw%20log).png)
