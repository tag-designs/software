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

## Existing Log format

The existing log format packed 16 submessages that were largely, but not exclusively raw bytes.  These were generated from 128 byte raw internal messages.

```protobuf
message IMUTagLogData{
  int32 pressure_raw = 1; // compact pressure; hPa = raw / 16; -1 means no sample
  int32 mx_raw = 2;       // compact mag; uT = raw * 0.04; all axes -1 means no sample
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

Because the existing log message consisted of 16 submessages that were largely packed bytes, we chose to merge these into a single 2048 byte "raw log" message format:

```protobuf
message IMUTagRawLog{
  int32 epoch = 1;
  int32 millisecond = 2;
  float temperature = 3;
  bytes samples = 4; // raw DATALOG_SAMPLES * t_DataLog page image
}
```

### both original and compressed formats assume the decoder respects little-endian data!!

## Revised performance

1) Time for full log download cycle: 17 ms
2) Time for full log_ack call: 3 ms
3) Time for Flash read: 2.27 ms
4) Time for Stlink to read protobuf containing log mesg: 12.8 ms

Now the dominant cost is the swd implementation.  Reformating the log message increased overall performance by almost 2x.

![Timing before optimization](../../docs/Log%20download%20cycle%20IMUTagBreakout.png)


![Timing after optimization](../../docs/Log%20download%20cycle%20IMUTagBreakout%20(raw%20log).png)

# Power Measurement

- idle:   150uA -- must be something floating
- 50hz:  1.06mA
- 100hz: 1.05mA
- 200hz: 1.2mA
- 400hz: 1.35mA
- 800hz: 1.69mA
- 1600hz: 1.7mA

There's a problem at idle and at finished -- I don't think the devices are shutdown and it doesn't look like there's a clear path where this would happen.  For example, after collecting data, I'm not sure that lptim2 is shut off and the IMU reset.  I don't know about the pressure sensor or magnetometer either.

