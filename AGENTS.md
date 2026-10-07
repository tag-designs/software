# AGENTS.md

This file gives coding agents a quick orientation to the software repository.
It is intentionally shorter than the READMEs: use it for working rules and
where-to-look guidance, then read the local README for details.

## Repository Shape

- `host/`: desktop host tools, command-line utilities, Qt applications, shared
  host libraries, and MkDocs user documentation.
- `embedded/`: ChibiOS firmware targets for tags and base/programmer boards,
  and SRAM-resident external flash loaders (`embedded/loaders`).
- `proto/`: shared protobuf definitions used by host tools and embedded nanopb
  generation.
- `docs/`: cross-cutting developer documentation (architecture, shared
  contracts, build, release, bench procedure, decisions, investigations), the
  generated index of every developer document at
  [`docs/index.md`](docs/index.md), and the developer portal scaffold in
  `docs/developer/`. Rules: [`docs/documentation-guide.md`](docs/documentation-guide.md).
- `include/`: C headers shared by host and firmware (monitor ABI, loader
  service block, packed log formats); see `docs/shared/`.
- `cmake/`: shared CMake helpers, presets support, vcpkg triplets, and package
  helpers.
- `ChibiOS/`: ChibiOS submodule. Do not edit it as project source.
- `archive/` directories contain retired or reference code. Ignore archived
  code when searching, refactoring, building, or reviewing unless the task
  explicitly asks about an archive.

## Where to Look First

Most questions about how this tree works are already answered in a README next
to the code. Read the relevant one before exploring; these are the entry points
that save the most time.

| Task | Read first |
| --- | --- |
| Adding or changing a tag target | `embedded/tags/README.md`, especially **Local Overrides** and **Template Tag Directory** |
| Understanding the firmware build, boards, or nanopb targets | `embedded/design/source-layout.md` |
| Adding a tag to the SQLite download path | the recipe comment at the top of `host/libraries/tagcore/sqlitelog.cc`, then `host/libraries/tagcore/sqlitelog/README.md` |
| Per-tag SQLite schema and row semantics | `host/docs/src/reference/sqlite-logs.md` (schema); `host/libraries/tagcore/sqlitelog/README.md` (writer rules) |
| Board pin and signal generation | `embedded/boards/README.md` |
| What a family shares and what a variant overrides | that family's `README.md` under `embedded/tags/families/` |
| Reading or erasing a tag's external flash without its firmware, or adding a loader | `embedded/loaders/README.md`, then `embedded/loaders/design/loader-runtime.md` |
| Design rationale for an existing subsystem | the nearest `design/` directory; every document is listed in `docs/index.md` |
| Measuring power, verifying a firmware change on hardware, debugging or capturing a tag | `docs/bench/README.md` |
| Why something was built the way it was | `docs/decisions/` (numbered records), then the owner's `design/investigations/` |
| Open work for a subsystem | the `TODO.md` in that subsystem's directory, e.g. `embedded/tags/TODO.md` |

Two conventions that are easy to miss and expensive to rediscover:

- A tag-local `./src/<name>.c` or `./inc/<name>.h` **replaces** the family or
  module file of the same basename, for that target only. This is how a variant
  diverges from its family without touching shared code.
- Runtime state that must survive standby lives in `pState`, a mirror of the RTC
  backup registers, not in ordinary statics.

## General Working Rules

- Prefer small, focused changes that match the existing directory ownership.
- Use `rg` for searches.
- Do not commit generated build products, package outputs, or local build trees.
- Do not edit `ChibiOS/` unless the task is explicitly about submodule
  management.
- Keep shared behavior in the lowest appropriate layer:
  - protobuf schema in `proto/`;
  - host protocol/log/download code in `host/libraries/tagcore`;
  - host sensor math in `host/libraries/sensoranalysis`;
  - reusable Qt/QML widgets in `host/libraries/sensorui`;
  - firmware board pin/signal ownership in `embedded/boards`;
  - firmware runtime behavior in `embedded/tags` or `embedded/bases`.
- Preserve user changes in the worktree. If unrelated files are dirty, leave
  them alone.
- Update nearby documentation when changing architecture, build behavior,
  packaging, or user-visible workflows.

## Build Orientation

The top-level CMake options are:

- `BUILD_HOST`: build host libraries/tools.
- `BUILD_QT_APPS`: build Qt GUI applications.
- `BUILD_EMBEDDED`: build embedded firmware targets.
- `BUILD_HOST_DOCS`: include MkDocs output in the default/package build.
- `BUILD_DEVELOPER_DOCS`: include the developer documentation portal in the
  default build. This is separate from packaged end-user host docs.

Prefer focused verification:

```sh
cmake --build <build-dir> --target sensorviz
cmake --build <build-dir> --target qtmonitor
cmake --build <build-dir> --target docs
cmake --build <build-dir> --target developer_docs
cmake --build <build-dir> --target api_docs
cmake --build <build-dir> --target PresTag
```

Use the target that matches the files changed. For documentation-only changes,
`git diff --check` is often enough unless CMake/docs build files changed.

### Firmware and bench work

Hardware procedure lives in [`docs/bench/`](docs/bench/README.md). **Read the
relevant document before changing the boot path, the state machine, power
sequencing, or the download path**: each rule below cost real time to learn,
and a clean build plus passing functional tests has repeatedly missed faults
these procedures catch.

- **Verify what you built.** With basename overrides the file you edited is not
  always the one compiled; clean-rebuild a target after changing a shared type;
  compare `.list` disassembly, never ELF checksums across commits.
  [Verifying a firmware change](docs/bench/verifying-firmware.md).
- **Measure after any change to `main.c` boot cleanup, `state_machine.c`,
  `pwr*.c`, `godown()`, `pState` or device power sequencing**, with
  `tag_lifecycle_check.py`, and qualify a release with `tag_release_check.py`.
  An argument is not a measurement.
  [Verifying a firmware change](docs/bench/verifying-firmware.md),
  [Power testing](docs/bench/power-testing.md),
  [Release procedure](docs/release/release-procedure.md).
- **Ask the user to detach the Joulescope desktop app and qtmonitor first, and
  wait for confirmation.** Never use the `joulescope-js220` MCP server; for any
  sweep run `joulescope_server.py`. [Power testing](docs/bench/power-testing.md).
- **An acknowledgement is not a completion, and an attach starts under reset.**
  Poll for the state you asked for; wait for a definite state before the first
  request. [Verifying a firmware change](docs/bench/verifying-firmware.md).
- **STM32U375 low-power entry is layout-sensitive.** Terminal sleep is Stop 3,
  run sleep is Stop 2 where `IMUTAG_RUN_SLEEP_STOP2` is set. When a change that
  cannot alter behaviour moves idle current, suspect layout before logic.
  [U375 low power](embedded/tags/common/core/design/u375-low-power.md).
- **Capture before anything resets or erases a failed tag**, with
  `tag-capture`, which halts the core before any firmware runs (the
  CubeProgrammer-based `tag_capture_state.py` lets the tag start booting, and is
  U375-only). Run `tag_rebuild_check.py` after any
  change to a family's `data_logAck()`, `readConfig()` or `system_logAck()`.
  [Capturing a tag](docs/bench/capturing-a-tag.md).
- **The flashing protocol, in order. Do not skip steps.**
  1. **Read the board UUID** (`tag-info`) and record it. A board swap looks
     exactly like a regression; "the same tag" is an assumption that expires
     the moment you stop watching the bench.
  2. **Erase the stored data before flashing**, whenever the tag is in
     `FINISHED` or `ABORTED`. The erase must be done by the firmware that wrote
     the log, which knows its own layout and cursors; a new image erasing
     someone else's log is interpreting a format it may not share. `tag-reset`
     erases, and only from those two states.
  3. **Flash.**
  4. **Run the self-tests** (`tag-test`) after flashing, before measuring or
     qualifying anything. It is the cheapest statement that the hardware and
     the image agree, and it costs under a minute.
- **A tag can latch a high resting current that only a power cycle clears.**
  On 2026-10-06 a PresTag sat at 0.2766 uA across three independently built
  images -- including a build of the exact source that had measured 0.1341 uA
  the day before -- and returned to 0.1249 uA after the board was unplugged and
  replugged. Nothing in the firmware was involved. **Before investigating an
  unexplained resting current, power cycle the board and measure again.** The
  likely culprits are a peripheral left awake by an erase, an attach storm, or
  a failed attach (which can leave a tag at hundreds of uA on its own).

- **DO NOT DESIGN A TEST WHERE THE RESET CHANGES WHAT YOU WANT TO SEE.** This
  comes before any other bench rule. Connecting is only possible through reset,
  so before writing any experiment, ask what the reset destroys. If the answer
  you are after is altered by the act of reading it, the test is invalid by
  construction and no amount of repetition will fix it -- repetition only makes
  a wrong answer look reproducible. Redesign the test so the thing you want
  survives, or measure it with something that needs no connection.
- **You cannot observe a running tag, on any target.** Connecting is only
  possible through reset -- on the STM32U375 as well as the STM32L432 -- so
  every attach (`tag-info`, `tag-capture`, the debugger) ends the run it was
  meant to observe. There is no live read anywhere in this tree.

  **Never re-attach to a running tag.** There are exactly two reasons to do it:
  you are specifically testing attach behaviour, or you want to stop the tag.
  Wanting to know what it is doing is not one of them -- that is the case where
  attaching destroys the answer. Anything a run needs to report, it reports
  after it ends. The evidence: a PresTag run polled eight times stored 3
  samples instead of ~30, and a UIUCTag run polled five times stored none,
  which then cost a day of wrong conclusions -- the tag was healthy all along
  and a current trace showed it waking and writing within minutes. The only genuinely passive instrument is a **Joulescope
  current trace**, which needs no connection and shows every wake. Everything
  else is post-mortem: let the run finish, then attach and read the epochs back
  from the download, or capture.
- **The scratchpad preserves state for debugging and nothing else.** It does
  not avoid the reset, it survives it: firmware writes into retained memory as
  it runs, the reset that attaching causes leaves it intact, and it is read
  afterwards. It answers "what happened before this tag stopped", never "what
  is this tag doing now". The same is true of plain RAM probes, since SRAM survives
  reset. **The retained scratchpad is STM32U375 only.** `TAG_SCRATCH_BASE` is
  `0x2003E000`, the last 8 KB of the U375's SRAM, held out of `ram0` by
  `STM32U375xG.ld`. On an STM32L432 that address is **outside RAM entirely** --
  SRAM1 is 48 KB at `0x20000000`, SRAM2 16 KB at `0x10000000` -- and
  `STM32L432xC.ld` reserves nothing, so every `tagScratch*()` call writes into
  the void. `-DTAG_SCRATCHPAD=1` on BitTag, PresTag, CompassTag or UIUCTag buys
  nothing. Plain RAM probes still work there, placed at a real L432 address and
  read after the reset that connecting causes, because SRAM survives reset.
  Never narrate from inside the idle or power path.
  [Debugging a tag](docs/bench/debugging-a-tag.md).
- **A failing check is a claim that needs the same scepticism as any other
  measurement.** Confirm what it measured before filing a firmware fault.

### Checking documentation coverage

Doxygen is configured but not currently part of a routine build. To check that a
change is documented to the standard below, generate with the project's own
configuration and **diff the warning log** — the tree carries a few thousand
pre-existing warnings, so the count alone means nothing:

```sh
sed -e "s|@PROJECT_SOURCE_DIR@|$PWD|g" -e "s|@[A-Z_]*@|x|g"     docs/developer/Doxyfile.in > /tmp/Doxyfile.check
printf '\nOUTPUT_DIRECTORY=/tmp/doxout\nWARN_LOGFILE=/tmp/doxwarn.txt\nGENERATE_HTML=NO\nQUIET=YES\n' \
    >> /tmp/Doxyfile.check
doxygen /tmp/Doxyfile.check
grep <your-file> /tmp/doxwarn.txt
```

`EXTRACT_ALL=NO` and `WARN_IF_UNDOCUMENTED=YES`, so anything undocumented is
reported. Note the INPUT paths cover `include/`, `host/libraries/*`,
`embedded/boards`, `embedded/loaders`, and `embedded/tags/{common,families}` — **individual tag
directories are not scanned**, so point Doxygen at one explicitly to check it.

### Tests

There is no test framework, no `enable_testing()`, and no CI test target. Where
logic can be checked without hardware, the established pattern is a standalone
assertion program built behind an option and run by hand; see
`host/libraries/tagcore/test/README.md` and
`embedded/tags/UIUCTag/test/README.md` for a worked example covering a shared
binary format, a firmware state machine compiled against stubs, and the two
meeting end to end.

## Cross-Cutting Notes

- Host SQLite logs are written by `host/libraries/tagcore` and viewed primarily
  by `host/applications/sensorviz`.
- SQLite log schema metadata should describe data: table names, columns, stream
  ids, labels, and units. Viewer policy such as colors, initial visibility, and
  axis side belongs in sensorViz display preferences.
- Protocol changes in `proto/` can affect host code, embedded nanopb code, and
  stored/default configuration JSON. Scope those changes carefully.
- If CMake source lists change for embedded firmware, update the relevant
  `BUILD_SOURCES.md` under `embedded/tags` or `embedded/bases`.

## Developer Documentation Maintenance

There are two documentation products. `host/docs/` is the end-user manual,
packaged with the host tools. Everything else is developer documentation,
rendered by the portal in `docs/developer/`.

**Read [`docs/documentation-guide.md`](docs/documentation-guide.md) before
adding or restructuring a developer document.** In short:

- Every document has one type (readme, design, decision, proposal,
  investigation, procedure, results, worklist) and YAML front matter giving its
  `type`, `status` and a one-line `summary`.
- A design document describes the shipped system only, answer first. History
  goes to a decision record in `docs/decisions/` or an investigation; plans go
  to `design/proposals/`.
- Documents live next to the code they explain. `docs/` holds only what no
  single directory owns: architecture, shared contracts, build, release, bench
  procedure, decisions and cross-cutting investigations.
- Never maintain a list of documents by hand. The index (`docs/index.md`), the
  portal sidebar and the portal staging list are generated from front matter by
  `docs/tools/docs.py`. After adding, moving or retitling a document, run
  `docs/tools/docs.py index`, then `docs/tools/docs.py check`.

Doxygen comments are the source of truth for API contracts. Markdown design
docs are the source of truth for architecture, rationale, tradeoffs, and
developer workflows. The developer portal copies/renders these sources; it
should not become the canonical place where design content is edited.

## Documentation Standards (C / C++ / CMake)

This project requires **production-quality Doxygen documentation** for project-owned, non-generated public APIs and important internal APIs. Documentation is not a formality — it must let a new engineer understand *what a component does, why it exists, and how to use it correctly* without reading the implementation.

### Scope Constraint: Documentation-Only Changes

**Documentation-only passes must never modify code behavior.** When an agent (or contributor) is asked only to document a file, the diff must contain comment additions/edits only — no changes to logic, formatting of code lines, variable names, includes, signatures, or whitespace outside of comment blocks.

- Do not "clean up while you're in there." If a bug, dead code path, or design issue is spotted while writing docs, note it in a `@todo` or `@bug` tag (or flag it separately to the author) — do not fix it in the same change.
- Do not reformat, reorder, or reflow existing code to make room for comments beyond the minimal insertion of the comment block itself.
- Do not change a function signature to add parameter names it was missing, even if that would make `@param` documentation cleaner — document the parameter as-is, or flag the missing name as a separate follow-up.
- If a comment cannot be written accurately without a code change (e.g. the current behavior is ambiguous or contradicts its name), stop and flag it rather than silently "fixing" the code to match the doc or vice versa.
- Diffs for a pure documentation task should be reviewable by scanning for `/** ... */`, `///`, `//!`, and `#[[ ... ]]` additions only. Any line outside a comment token that changes is out of scope and should be reverted or split into a separate change.

### General Principles

- Every project-owned, non-generated header file, class, struct, enum, function, macro, and non-trivial variable gets a Doxygen comment block.
- Write for the reader who has domain knowledge (embedded systems, RTOS internals, peripheral registers) but zero knowledge of *this specific codebase*.
- Document **behavior and contracts**, not syntax. Never restate the signature in prose (e.g. don't write "takes an int and returns a bool").
- State units, ranges, and unrepresentable values explicitly (`uint32_t timeout_ms`, not `uint32_t timeout`).
- Document failure modes: what happens on invalid input, timeout, or hardware fault — not just the happy path.
- If a function has side effects (register writes, ISR state, DMA ownership, blocking behavior), state them.
- Prefer `@brief` + a short paragraph over a wall of text. Long explanations belong in `@details` or a `.md` design doc referenced via `@see`.

### File-Level Documentation

Every project-owned, non-generated `.h` / `.hpp` / `.c` / `.cpp` starts with a file block:

```c
/**
 * @file    lptim_systick.c
 * @brief   LPTIM-backed ChibiOS system tick driver for STOP-mode retention.
 *
 * @details Implements the OSAL system timer using LPTIM2 clocked from LSE,
 *          allowing the RTOS time base to continue advancing through
 *          Stop0/Stop1/Stop2 low-power modes. Falls back to the default
 *          SysTick-based timer if LPTIM2 is unavailable or misconfigured.
 *
 * @note    Requires LPTIM2 kernel clock sourced from LSE (32.768 kHz).
 *          Do not combine with HAL_LPTIM usage on the same instance.
 */
```

### Function Documentation

Every function declared in a header and every `static` function with non-obvious behavior or an important local contract in a `.c`/`.cpp` file requires:

```c
/**
 * @brief   Configures and arms the LPTIM-based tick source.
 *
 * @details Programs LPTIM2 in continuous mode with autoreload derived from
 *          @p tick_hz, unmasks the associated EXTI line for STOP-mode
 *          wakeup, and enables the compare interrupt. Must be called before
 *          the scheduler starts; calling it after @c chSysInit() results in
 *          undefined tick timing.
 *
 * @param[in] tick_hz   Desired OS tick frequency in Hz. Must divide evenly
 *                       into the LSE-derived LPTIM clock (typically 32768 Hz
 *                       / prescaler). Values that don't divide evenly are
 *                       rounded down silently.
 *
 * @return  true if LPTIM2 was armed successfully, false if the clock source
 *          was not LSE or the peripheral was already in use.
 *
 * @pre     RCC clock tree must have LPTIM2 kernel clock enabled and routed
 *          to LSE via CCIPR before calling.
 * @post    LPTIM2 interrupt (LPTIM2_IRQn) is enabled in NVIC.
 *
 * @warning Not safe to call from an ISR context.
 *
 * @see     lptim_systick_stop(), RCC_CCIPR1_LPTIM2SEL
 */
bool lptim_systick_start(uint32_t tick_hz);
```

Tag conventions:
- `@param[in]`, `@param[out]`, `@param[in,out]` — always specify direction.
- `@return` — describe every distinct return value/state, not just "success/failure."
- `@pre` / `@post` — required whenever the function depends on or changes global/peripheral state (clock trees, DMA ownership, RTOS phase).
- `@warning` — required for ISR-safety, reentrancy, blocking, or ordering hazards.
- `@note` — non-critical clarifications (e.g. performance characteristics, deprecated paths).
- `@see` — cross-reference related functions, registers, or design docs.

### Class / Struct Documentation

```c++
/**
 * @class   Bmm350Driver
 * @brief   SPI/I2C driver for the Bosch BMM350 magnetometer.
 *
 * @details Wraps the vendor BMM350 SensorAPI with a float-based compensation
 *          path (bypassing the vendor's Q48.16 fixed-point compensation,
 *          which is slower than float on FPU-equipped Cortex-M cores).
 *          Not thread-safe; callers must serialize access if the driver
 *          instance is shared across contexts.
 */
class Bmm350Driver {
public:
    /**
     * @brief   Reads one magnetometer sample.
     * @param[out] sample   Populated with compensated µT values on success.
     * @return  true on success, false on bus error or data-not-ready.
     */
    bool readSample(MagSample& sample);

private:
    float compensation_coeffs_[8]; ///< Cached OTP compensation coefficients.
};
```

- Member variables get trailing `///<` comments when the name alone doesn't convey units/meaning.
- Document class invariants (e.g. "must be initialized via `init()` before any other call") in the class-level `@details`, not scattered across methods.

### Enums and Macros

```c
/**
 * @enum    stop_mode_t
 * @brief   Supported STM32U375 low-power STOP modes for this driver.
 */
typedef enum {
    STOP_MODE_0, ///< Fastest wakeup, highest retained current.
    STOP_MODE_1, ///< SRAM retained, moderate wakeup latency.
    STOP_MODE_2, ///< Lowest current; peripherals lose autonomous operation.
} stop_mode_t;

/**
 * @def     LPTIM_MAX_ARR
 * @brief   Maximum autoreload value for LPTIM (16-bit counter).
 */
#define LPTIM_MAX_ARR 0xFFFFu
```

### CMake Documentation

CMake isn't Doxygen's native domain, but apply the same rigor using a consistent human-readable comment convention (`#[[ ... ]]` block comments). If the build later adds a Doxygen filter for CMake, keep these comments structured enough to be converted into module reference material:

```cmake
#[[
  @brief  Configures the ChibiOS build for a given STM32 target.

  @details Adds the HAL, RT, and board-support sources for TARGET_MCU,
           sets up the linker script from BOARD_LD_SCRIPT, and defines
           the STM32Uxx-family compile definitions required by ChibiOS'
           os/hal/ports layer.

  @param   TARGET_NAME    Name of the executable target to configure.
  @param   TARGET_MCU     MCU family string, e.g. "STM32U375" or "STM32L432".
  @param   BOARD_LD_SCRIPT Path to the linker script for this board.

  Example:
    configure_chibios_target(my_logger STM32U375 boards/u375/link.ld)
#]]
function(configure_chibios_target TARGET_NAME TARGET_MCU BOARD_LD_SCRIPT)
    ...
endfunction()
```

Rules:
- Every `function()` / `macro()` in shared `.cmake` modules gets a block comment following the same `@brief`/`@param`/`@details`/example structure as C functions.
- Document **why** a flag or option exists when it isn't self-evident (e.g. `option(USE_HW_FPU "Enable hardware FPU compensation path for BMM350" ON)` should have a one-line comment above it explaining the tradeoff, not just restating the option string).
- `CMakeLists.txt` files that configure a whole target/board get a short header block comment describing the target's purpose and any non-obvious dependencies (e.g. "requires arm-none-eabi-gcc >= 12, links against ChibiOS RT + HAL for STM32U3xx").

### What NOT to Do

- Don't write comments that just restate the function/variable name (`// increments counter` above `counter++`).
- Don't leave `@param` or `@return` blank or with placeholder text — if a parameter is genuinely self-explanatory, it can be omitted from prose but the tag stays with a real one-line description.
- Don't document implementation details that will drift out of sync with the code (e.g. "loops 4 times" for a size that isn't fixed) — document behavior/contract instead.
- Don't skip documentation on important `static`/internal functions just because they're not part of the public API — undocumented internals are exactly where new engineers get lost.

### Doxyfile / Build Integration

- Enable `EXTRACT_ALL = NO` (undocumented entities should be visibly flagged, not silently included) and `WARN_IF_UNDOCUMENTED = YES` so missing docs surface as build warnings.
- Enable `OPTIMIZE_OUTPUT_FOR_C` for pure-C modules and unset it for C++ modules/mixed targets.
- If using CMake to drive Doxygen generation, wire warnings into CI as non-fatal initially, then promote to fatal once the codebase reaches full coverage.
