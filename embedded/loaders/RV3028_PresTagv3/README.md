---
type: readme
status: current
summary: Read-only SRAM probe that reads RV3028 registers over I2C, chiefly to recover the factory EEOffset clock calibration.
---

# RV3028_PresTagv3: RTC register probe

An SRAM-resident image that reads the RV3028 RTC's registers over I2C, on a tag
with no firmware or firmware that cannot run. Its main use is reading the
part's **factory EEOffset** (registers 0x36 and 0x37), the clock calibration
that the firmware stores with each session (B2 in
[firmware TODO](../../tags/TODO.md)).

It is read-only by construction. It performs one register-read transaction: a
write of the register pointer, a repeated start, then a read. It never writes a
register, never issues an EEPROM command and never sets EERD. The factory
offset cannot be recovered once lost, so keep it that way.

On a part that has never run firmware, the RAM mirror at 0x30-0x37 holds the
configuration EEPROM, copied about 66 ms after power-up. Probing boards before
they are first programmed therefore records each tag's factory offset.

PresTagv3 and CompassTagv1 both wire the RV3028 to PB6 (SDA) and PB7 (SCL), so
the image works on either. UIUCTag swaps the two lines, so it uses
`RV3028_UIUCTag`, built from the same source (`../common/src/rv3028_probe.c`)
with `-DPROBE_SWAP_I2C=1`. It is built with the loader framework (no crt0,
HSI16 by hand, bounded waits), keeps only the clock and delay code, and touches
RCC, the flash latency, and GPIOB pins 6 and 7.

```sh
build-host/bin/tag-sramcall \
    --image build-host/embedded/loaders/RV3028_PresTagv3/RV3028_PresTagv3/build/RV3028_PresTagv3.elf \
    --call Rv3028ReadRegs --args buf,0x30,8 --dump 8
```

`Rv3028ReadRegs(buf, first, count)` returns 1, or a negative code: bus stuck
(-2), no ACK to the address (-3, -5) or the register (-4), or a bad range
(-6).

Decode EEOffset as `(reg36 << 1) | (reg37 >> 7)`, a signed 9-bit value in
steps of 0.9537 ppm. A blank PresTag on 2026-10-01 read `FF`/`10`, which is
−2 steps or −1.907 ppm. A UIUCTag read `00`/`90`, +1 step or +0.954 ppm.
