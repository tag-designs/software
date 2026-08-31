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
