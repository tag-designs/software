# UIUCTag Sequencer Simulation

`sequencer_sim.c` compiles the real `../src/state_run.c` for the host, against
the minimal stubs in `stub/`, and drives `Running()` over a synthetic
minute-alarm timeline.

It exists because the UIUCTag acquisition sequencer is almost entirely
arithmetic over the acquisition clock — which slot a sample lands in, when its
activity word follows, when a block rolls over, what happens after a reset — and
none of that needs a tag to be wrong. Reproducing a two-hour block rollover, a
mid-block reset, and a multi-hour hibernation window on hardware takes most of a
day; here it takes a second.

The fake external flash asserts on the two faults that are painful to diagnose
on a real tag:

- programming a NOR word twice, which silently ANDs bits rather than failing;
- writing while the flash is in deep power-down.

## Build and run

```sh
cd embedded/tags/UIUCTag
cc -std=c11 -Wall -Wextra -o /tmp/sequencer_sim test/sequencer_sim.c \
   -Itest/stub -Itest -I../../../include -Iinc -Isrc
/tmp/sequencer_sim /tmp/uiuc_sim_blocks.bin
```

It is not part of any build target: it includes a `.c` file and a stub tree that
must not reach firmware, and keeping it out of CMake is the simplest way to
guarantee that. Expect `FIRMWARE SEQUENCER SIM: all assertions passed`; any
failure aborts on the assertion that describes it.

The optional argument names a file to receive the simulated external-flash block
images. Feed that file to `uiuctag_end_to_end_check` in
`host/libraries/tagcore/test` to decode firmware-produced bytes with the real
host decoder.

## Datalog simulation: filling the flash

`datalog_sim.c` complements the sequencer simulation. Where `sequencer_sim.c`
replaces the datalog layer with a recorder, this harness compiles the real
`../src/datalog.c` together with `../src/state_run.c`. It links them to a fake
external NOR and a fake internal checkpoint array, and drives the minute alarm
until the flash is full. Then it downloads every block through the real
`data_logAck()`.

Its subject is fix A3 from the `firmware-fix` branch
(see [next-release-todo.md](../../design/next-release-todo.md)). Flash size is
not a multiple of the 288-byte block, so the end of the part cuts the final
block short. That block must still be served, with every slot that was
completed plus the fields of the slot cut short that fit:

- on 4 MiB (AT25XE321D), 13 slots plus the cut-short slot's pressure;
- on 8 MiB, 2 slots plus the cut-short slot's pressure and temperature.

fw-v0.0.3 served only whole blocks. The fake NOR also asserts on any read past
the end of the part.

```sh
cd embedded/tags/UIUCTag
cc -std=c11 -Wall -Wextra -Wno-pointer-to-int-cast -O1 \
   -o /tmp/uiuctag_datalog_sim test/datalog_sim.c \
   -Itest/stub -Itest -I../../../include -Iinc -Isrc
/tmp/uiuctag_datalog_sim
```

The run should print `UIUCTAG DATALOG SIM: all assertions passed`. Built
against the `datalog.c` from before A3, it fails with `block not served`.

`-Wno-pointer-to-int-cast` is needed because `datalog.c` compares flash
addresses as `uint32_t`, which is exact on the 32-bit tag. On a 64-bit host the
harness asserts that its checkpoint array does not straddle a 4 GiB boundary.

The register blocks, kernel calls and `UIUCTagLog` Ack that `datalog.c` needs
are defined in the harness itself. `stub/` gained only `flash_internal.h` and
`storage_flash.h`, which `sequencer_sim.c` never includes, so the stubs it
relies on are unchanged.

## What it does not cover

Sensors, buses, power, and real timing are all stubbed. The simulation is only
meaningful because the sequencer's decisions are separable from those; see the
[test strategy](../../families/BitPresTag/design/uiuctag-test-strategy.md) for
what is deliberately left to a hardware run.

## Stubs

`stub/` holds the smallest set of declarations `state_run.c` needs: the
`BackupState` mirror, the stored-configuration struct, the log error enum, and
the handful of protobuf enum values it references. They are hand-written rather
than generated, so if `state_run.c` starts using something new, the simulation
fails to compile until the stub is extended — which is the intended signal, not
an inconvenience.
