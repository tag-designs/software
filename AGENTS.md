# AGENTS.md

This file gives coding agents a quick orientation to the software repository.
It is intentionally shorter than the READMEs: use it for working rules and
where-to-look guidance, then read the local README for details.

## Repository Shape

- `host/`: desktop host tools, command-line utilities, Qt applications, shared
  host libraries, and MkDocs user documentation.
- `embedded/`: ChibiOS firmware targets for tags and base/programmer boards.
- `proto/`: shared protobuf definitions used by host tools and embedded nanopb
  generation.
- `design/`: developer architecture specifications and design notes. The master index is located at [**`design/index.md`**](file:///design/index.md).
- `docs/developer/`: developer documentation portal scaffold. CMake stages
  curated source-adjacent design notes and Doxygen output into the build tree.
- `cmake/`: shared CMake helpers, presets support, vcpkg triplets, and package
  helpers.
- `ChibiOS/`: ChibiOS submodule. Do not edit it as project source.
- `archive/` directories contain retired or reference code. Ignore archived
  code when searching, refactoring, building, or reviewing unless the task
  explicitly asks about an archive.

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

This repository has two documentation products:

- `host/docs/`: end-user application manuals and workflow guides. These are
  built into host distribution packages.
- `docs/developer/`: developer architecture, design, and API documentation.
  This is a local/CI browser portal for understanding the codebase and is not
  the packaged user manual.

Keep developer design documents close to the code they explain:

- Cross-cutting architecture, repo-wide contracts, protocol policy, build
  policy, and ADRs belong under top-level `design/`.
- Firmware/platform design belongs near firmware code, for example
  `embedded/design/`, `embedded/tags/design/`, or a target/module-local
  `design/` directory.
- Host-library and application design belongs near the owning host directory,
  for example `host/libraries/tagcore/README.md` or
  `host/applications/sensorviz/design/`.
- Do not move local design docs to top-level `design/` merely to make them
  discoverable. Add or update navigation stubs instead.

Whenever you add, move, rename, or materially update a developer design
document:

- Update the nearest local `README.md` or `design/index.md` so the document is
  discoverable from its owning subtree.
- Update top-level `design/index.md` when the document is architecture-relevant,
  cross-cutting, or useful to engineers outside the owning subtree.
- Update `DEVELOPER_DOCS_MARKDOWN` in top-level `CMakeLists.txt` and
  `docs/developer/src/source-tree.md` when the document should appear in the
  browser portal.
- Update `docs/developer/mkdocs.yml` only for curated navigation entries that
  should be visible in the sidebar. The sidebar should first mirror repository
  ownership (`design/`, `embedded/`, `host/`, `proto/`) and then provide
  cross-cutting topic views.
- Keep topic sections organized by scope: cross-cutting architecture,
  build/tooling, embedded and firmware platform, host libraries/applications,
  protocols/data formats, active decisions, and historical notes.
- Prefer one-line index entries with a short "why this exists" description.
- Mark stale or historical docs explicitly instead of silently leaving them in
  active sections.

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
