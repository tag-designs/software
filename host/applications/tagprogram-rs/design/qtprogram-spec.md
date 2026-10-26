---
type: results
status: current
summary: A functional and visual specification of qtprogram (Tag Programmer v2.0), read from its source and manual, that the Rust port reproduces.
---

# qtprogram (Tag Programmer v2.0): functional and visual specification

Base directory: `/Users/geobrown/Research/tag-designs/software/host/applications/qtprogram/` (written as `Q/` below).

## Files read
Every file in `Q/` except `Doxyfile` (the Doxygen config):
- Sources: `main.cpp`, `mainwindow.{h,cpp}`, `station.{h,cpp}`, `programsession.{h,cpp}`, `programmercli.{h,cpp}`, `flashplan.{h,cpp}`, `firmwareimage.{h,cpp}`, `firmwarecatalog.{h,cpp}`, `recordlog.{h,cpp}`, `logwindow.{h,cpp,ui}`
- Build and docs: `CMakeLists.txt`, `README.md`, `design/batch-station.md`, `test/README.md`, `test/qtprogram_checks.cpp`
- There are no `.qrc` or QML files. `CMakeLists.txt:74` has a commented-out `resources.qrc`.
- Shared host_common headers it uses:
  - `/Users/geobrown/Research/tag-designs/software/host/common/qthoststyle.h`
  - `/Users/geobrown/Research/tag-designs/software/host/common/qtuserguide.h`
  - `/Users/geobrown/Research/tag-designs/software/host/common/qtfiledialog.h`
- tagcore: `host/libraries/tagcore/log.{h,c}`, `tagclass.h`, `linkadapt.h`
- User manual: `/Users/geobrown/Research/tag-designs/software/host/docs/src/apps/qtprogram.md`. It is linked from `host/docs/src/index.md:8`.

## Screenshots
- `/Users/geobrown/Research/tag-designs/software/host/docs/src/images/qtprogram-station.png` (manual line 53)
- `/Users/geobrown/Research/tag-designs/software/host/docs/src/images/qtprogram-program.png` (manual line 84)
- `host/docs/src/reference/documentation-guidelines.md:60,72` refers to `qtprogram-main-window.png`. That file does not exist.

**What `qtprogram-station.png` shows** (macOS native style, about 900×697):
- Title "Tag Programmer v2.0". An in-window menu bar with "Help" then "Station".
- Firmware combo reads "fw-v0.7  (2026-10-15)", with a "Release Notes" button.
- Tag type combo reads "BitTag", with the note "detected from the tag".
- The "Station" group box has 8 green rows:
  - Programmer: STM32CubeProgrammer v2.22.0
  - Base: ST-LINK 0037001A5556431520303537
  - SWD: 0x435 STM32L43xxx/STM32L44xxx
  - Supply: 3.23 V
  - Tag: BitTag V6, 169cda2c
  - Tag state: IDLE
  - Firmware: BitTag (fw-v0.7), verified against its manifest
  - Record: programmed-tags.csv (1 row)
- The "Tag" group box shows:
  - Label: BitTag-bench (recorded)
  - UUID: 2035374D303150190059002F
  - State: IDLE
  - Firmware: BitTag  169cda2c  (BitTag V6)
  - Tests: ALL_PASSED
  - Result: empty
- **The screenshot has no Clock/Sync row, so it predates the current code.**
- Button row: Program (blue, default), Retest, Next Tag, Attach, then Show CSV at the right.
- Bottom: "Batch: 0 programmed this session, 0 not OK" and a "Log" toggle with a right arrow.

**What `qtprogram-program.png` shows** (dialog "Program Tag"):
- Label: BitTag-017 (selected)
- Tag: BitTag  IDLE  UUID 2035374D303150190059002F  3.23 V
- Image: "BitTag (fw-v0.7), commit 169cda2c" / "SHA-256 18d6fc13…7a8ac"
- Erase: Mass erase and program (STM32L432 with no calibration page)
- Lost: two bullets (log; stored configuration)
- Kept: "• The RTC trim and the board UUID."
- Amber warning: "This tag is recorded as BitTag-bench."
- Buttons: Cancel, Erase and Program (drawn blue by macOS).

---

## 1. Window layout

### Start-up and style (`Q/main.cpp:140-229`)
- Organization `tag-designs`, application `qtprogram`.
- `HostStyle::apply` (`qthoststyle.h:312-332`):
  - On macOS it keeps the native style unless `TAG_HOST_STYLE` is set.
  - Otherwise it uses Fusion plus `lightPalette()` and the `lightStyleSheet()` defined at `qthoststyle.h:23-293`. Key colours:
    - window #ececec
    - QGroupBox: background #dedede, border 1px #b4b4b4, radius 4, margin-top 1.25em, padding 8
    - group title: left 8px
    - fields: white, border #a8a8a8, radius 3, padding 2px 4px; focus border #1f6fd0
    - QPushButton: white, border #9c9c9c, radius 4, padding 4px 12px; hover #f2f2f2; pressed #d6d6d6; default-button border #1f6fd0; disabled #ebebeb with text #8a8a8a
    - menubar #e4e4e4
    - QProgressBar chunk #1f6fd0
    - text #1a1a1a
  - `TAG_HOST_STYLE` = system, native or none leaves the platform style alone.
- Then: `log_set_quiet(true)`, `log_set_level(log_level)` and `log_add_callback(..., LOG_TRACE)`.
- `MainWindow w; w.show();` then `QTimer::singleShot(0, start)`.

### Window (`Q/mainwindow.cpp:107-161, 163-332`)
- Title: `"Tag Programmer v2.0"` (macro `title_string`, line 47).
- `resize(900, 0)`: 900 wide, minimum height.
- No toolbar, no status bar, no icons. The layout is built in code.

**Menu bar.** It is in the window, not native (`setNativeMenuBar(false)`, `qtuserguide.h:136`). Help is added first, so it appears left of Station.
- **&Help** (`qtuserguide.h:132-154`)
  - "Tag Programmer User Guide". Tooltip: "Open the Tag Programmer page of the user manual". It opens `apps/qtprogram.html` or `apps/qtprogram/index.html` from the doc roots. If neither is found it shows an information box:
    - title "Tag Programmer User Guide"
    - text "The Tag Programmer User Guide was not found."
    - informative "Build the host documentation target and try again:\ncmake --build <build-dir> --target docs"
    - details "Looked in:\n…"
  - "About Tag Programmer": `QMessageBox::about` with "Tag Programmer v2.0\n\nProgram and test a batch of tags through a USB base, recording each one." (`mainwindow.cpp:111-114`)
- **&Station** (`mainwindow.cpp:116-132`)
  - "Attach Automatically": checkable, initial state = setting `resetsAcknowledged`. Toggling calls `Station::setAutoAttach`.
  - "Choose Programmer..." calls `chooseProgrammer()`.
  - "Choose Base...": if `listBases()` returns false, shows an information box "Choose a Base" / "No ST-LINK is on USB, or programming is in progress." Otherwise calls `chooseBase()`.
  - "Choose Record File..." calls `chooseRecord()`.
  - "Refresh Firmware Releases" calls `catalog().refresh()`.

**Central widget tree** (one QVBoxLayout):

1. **QGridLayout "choice"** (lines 169-190); column 1 stretches.
   - (0,0) QLabel "Firmware"
   - (0,1) `firmwareBox_` QComboBox, `setMinimumContentsLength(32)`
   - (0,2) QPushButton "Release Notes". It opens `catalog.release(chosenRelease).pageUrl` in the browser. It is enabled only when that pageUrl is non-empty.
   - (1,0) QLabel "Tag type"
   - (1,1) `targetBox_` QComboBox
   - (1,2) `targetNote_` QLabel: "from the image" when a file is chosen, "detected from the tag" when the target was detected, otherwise empty.
2. **QHBoxLayout "panels"**: the Station box and the Tag box, stretch factors 3 and 2.
   - **QGroupBox "Station"** with a QGridLayout; column 2 stretches. One row per check, i in 0..7:
     - col 0: QLabel "●" (U+25CF), top-aligned. Its colour is set by stylesheet `color: <c>;`:
       - Ok #2e7d32
       - Warn #c77700
       - Fail #c62828
       - Pending #9e9e9e
     - col 1: QLabel with the check name, top-aligned. Names in order: "Programmer", "Base", "SWD", "Supply", "Tag", "Tag state", "Firmware", "Record" (lines 53-57).
     - col 2: text QLabel. Word wrap on; minimum height of 2 line spacings; aligned left/top; selectable by mouse. Its text is the status text, or "—" (em dash) when empty. Its tooltip is the full text.
     - col 3:
       - Programmer row: QPushButton "Choose...". Visible only when the Programmer level is not Ok.
       - Record row: "Choose...". Visible only when the Record level is not Ok.
       - Tag state row: an HBox with "Stop" and "Erase".
   - **QGroupBox "Tag"** with a QFormLayout. Rows, all value labels selectable and single-line:
     - "Label"
     - "UUID"
     - "State"
     - "Firmware"
     - "Tests"
     - "Clock": HBox with the value label (stretch 1; tooltip "The tag's clock minus this computer's, in seconds") and QPushButton "Sync" (tooltip "Set the tag's clock to this computer's")
     - "Result"
3. **Actions HBox**: "Program" (`setDefault(true)`), "Retest", "Next Tag", "Attach", a stretch, then "Show CSV". Show CSV opens the record file's folder via `QDesktopServices`.
4. **Session HBox**:
   - `stepLabel_`: word-wrapped, stretch 1
   - QProgressBar: max width 200, hidden
   - "Retry": hidden
   - "Give Up": hidden; its text switches between "Give Up" and "Cancel"
   - Retry calls `session.retryConnect`; Give Up/Cancel calls `session.cancel`.
5. **Bottom HBox**:
   - `batchLabel_` (stretch 1): `"Batch: %1 programmed this session, %2 not OK"`
   - QToolButton "Log": checkable, text beside icon, `Qt::RightArrow`, which becomes `DownArrow` when on.
6. **LogWindow** (hidden at first, stretch 1). Toggling Log shows or hides it. When it is hidden, the window calls `adjustSize()` on the next event-loop pass.

**Display rules**
- `showCheck` (lines 352-367) draws one row, as described above.
- `showTag` (lines 453-482):
  - Label is `station.lastLabel()`.
  - If the tag does not answer: UUID, State, Firmware, Tests and Clock show "—", and the clock stylesheet is cleared.
  - Otherwise:
    - UUID = `info.uuid`
    - State = `TagState_Name`
    - Firmware = `"%1  %2  (%3)"` with tagTarget, githash and board_desc (two spaces between parts)
    - Tests = `TestResult_Name(test_status)`
    - Clock = `"%+.2f s"` if |offset| < 1000, else `"%+.3g s"`. If |offset| > 2.0 it appends `"  (press Sync)"` and turns amber (#c77700).
    - If the record has labels for this UUID, Label becomes `"<last label> (recorded)"`.
- `showFirmware` (lines 369-429): the firmware combo is rebuilt each time. Item data uses the prefixes `release:<tag>`, `file:<path>` and `other`.
  - One item per catalog release, newest first. Text is the tag, plus `"  (yyyy-MM-dd)"` if a publish date is known, plus `"  prerelease"`, plus `"  not downloaded"` when it is not cached.
  - If the chosen release is not in the list, it is appended as plain text.
  - A separator follows if there are any recent files or a chosen file.
  - File items: the chosen file (prepended if not already in recents) and the recent files. Text is `"<file name>  (<native absolute dir>)"`.
  - Then a separator and "Other ELF file...".
  - If nothing is current, "Choose firmware" (empty data) is inserted at index 0 and selected.
  - Tag type combo: `station.targets()`, plus the current target if it is not in that list. Current index is -1 when there is no target. The combo is enabled only when a release is chosen.
- `firmwareChosen` (lines 431-451): a release item calls `chooseRelease`, a file item calls `chooseFile`, and "other" opens a file dialog. If that dialog is cancelled the combo is refilled.
- `updateButtons` (lines 492-513):
  - Program: enabled = `canProgram(&why)`. Its tooltip is `why` when disabled.
  - Retest: enabled when no session is running, the tag answers, and Record is Ok.
  - Next Tag: enabled when no session is running.
  - Attach: enabled when no session is running and Programmer is Ok.
  - Firmware combo: enabled when no session is running.
  - Tag type combo: enabled when no session is running and a release is chosen.
  - Stop: visible when the tag answers and its state is not IDLE, FINISHED, ABORTED, TEST or sRESET. Enabled when no session is running.
  - Erase: visible when the tag answers and its state is FINISHED or ABORTED. Enabled when no session is running.
  - Sync: enabled when no session is running and the tag answers.
- `sessionStep` (lines 732-745):
  - Step label = the step text while running; empty at Done.
  - Progress bar: visible while running except at PowerCycle. Range is reset to (0,0), the busy indicator, on each step.
  - Retry: visible only at PowerCycle.
  - Cancel button: visible at PowerCycle, Stopping and Erasing. Its text is "Give Up" at PowerCycle and "Cancel" otherwise.
  - `eraseProgress(done,total)` sets the range to (0, total, or 0 if unknown) and the value to `done`.
- `programFinished` (lines 747-760):
  - Result = `record.result`, or the message when the result is empty. Green if the row was written and result == "OK", red otherwise.
  - The step label shows the message.
  - If not OK, a warning box opens: title "Program Tag", text = the message.

### Log pane (`Q/logwindow.ui`, `Q/logwindow.cpp`)
- VBox containing:
  - QTextEdit `logTextEdit`: read-only, undo off, tooltip "log of error messages".
  - HBox:
    - QPushButton "Save Error Log" (tooltip "Save error log to file")
    - spacer
    - QLabel "Log Level"
    - QComboBox (tooltip "Set error logging level") with items TRACE, DEBUG, INFO, WARN, ERROR. FATAL is excluded. Default is INFO.
- Changing the level calls `log_set_level(index)` and sets the global `log_level`.
- **Effective start level is INFO.** `main.cpp:41` sets `log_level = LOG_ERROR`, but the LogWindow constructor's `setCurrentIndex(LOG_INFO)` fires the auto-connected slot and overrides it.
- Save:
  - `getSaveFileName(this, "Save Log", home)` with no filter.
  - The file is opened ReadWrite, without truncation, and the plain text is written.
  - On failure: critical box "Error" / "Cannot open file".
- Routing (`main.cpp:50-117`):
  - The Qt message handler maps Info→INFO, Debug→DEBUG, Warning→WARN, Critical→ERROR, Fatal→abort.
  - tagcore events are formatted with `vasprintf`.
  - A line is appended when `level >= log_level`:
    - with a source line: `"<LEVEL> <function> <line>:<msg>"`
    - without one: `"<LEVEL> <msg>"`
- **The programmer's output is not shown in the log pane.** `ProgrammerCli::outputLine` (`programmercli.h:188`) is emitted but connected nowhere. The pane only gets "Running: <path> <args>" (`programmercli.cpp:201`) and the session's qInfo/qWarning lines. The user messages "Its output is in the log" (`programsession.cpp:210`, manual line 153) are therefore not true.
- On macOS all file dialogs are Qt's own dialogs, not native (`qtfiledialog.h:13-20`).

---

## 2. The Station model (`Q/station.h`, `Q/station.cpp`)

**Checks** (`enum class Check`, `station.h:43-53`; `kCheckCount = 8`):
Programmer, Base, Swd, Supply, Tag, TagState, Firmware, Record.

**Levels** (`station.h:59-65`):
- Pending: grey
- Ok
- Warn: passes, but needs attention
- Fail: blocks Program

A `CheckStatus` is `{level, text}`; all start Pending with empty text. `set()` emits `checkChanged`.

**Constants**
- `kRecentCount` = 6
- `kL432MaxVolts` = 3.6
- `kLowVolts` = 1.8
- `kMissedPolls` = 5
- `kAttachTries` = 10
- `kAttachRetryMs` = 200 (lines 31-40)
- `kClockToleranceS` = 2.0 (`station.h:143`)
- Device IDs: L432 = 0x435, U375 = 0x454 (`programmercli.h:28-30`)

**Timers**
- `basePoll_`: 1000 ms, calls `checkBase`.
- `statusPoll_`: 400 ms, calls `poll` (lines 78-81).
- The session's own tick is 1000 ms.

**begin()** (lines 142-155):
1. `checkProgrammer()` (asynchronous)
2. `checkBase()`
3. `catalog.refresh()`
4. `loadImage()`
5. Open the record at the saved `recordPath` (a failure is logged with qWarning).
6. `checkRecord()`; if no record is open, emit `recordNeeded`, which opens the Record dialog.

Before `begin`, `MainWindow::start` (lines 334-350) asks once, if the setting `resetsAcknowledged` is missing:
- Question box "Attach Automatically?", buttons Yes/No, default No.
- Text: "Tag Programmer can connect to each tag as soon as it finds the base, so its state and identity are shown without pressing Attach.\n\nConnecting resets the tag, which ends a deployment that is running. Attach automatically from now on? (Station > Attach Automatically changes this.)"
- The answer is saved.

**Programmer check** (lines 159-182):
- Path = `findProgrammer(setting "programmer")`.
- None found: Fail "STM32_Programmer_CLI not found. Choose the STM32CubeProgrammer folder."
- Otherwise Pending "Checking <path>" and run `--version`.
- Exit code ≠ 0, or no version parsed: Fail "<path> does not run. Choose the STM32CubeProgrammer folder."
- Success: save the path; Ok "STM32CubeProgrammer v<ver>". Then, if a base is known, auto-attach is on, Swd is Pending and nothing is attached, call `attach()`.
- `setProgrammerDirectory` (lines 184-192): `programmerInDirectory(path)`; save it and re-run the check, or return false.
- In `chooseProgrammer` (lines 515-533) it is a file dialog:
  - macOS: starts in `/Applications`, caption "Choose STM32CubeProgrammer.app or STM32_Programmer_CLI".
  - Elsewhere: starts in home, caption "Choose STM32_Programmer_CLI (in STM32CubeProgrammer's bin folder)".
  - On failure: warning "STM32CubeProgrammer" / "STM32_Programmer_CLI is not <path>, nor inside it."

**Base check** (lines 196-216). It returns at once if a session is running or the tag is attached.
- `tag.Available(bases_)`.
- None: Warn "No base on USB. Plug in the base." and start `basePoll_`.
- Otherwise stop the poll, then:
  - If there is one base, or a base whose serial equals the saved `baseSerial`, call `chooseBase(i)`.
  - Otherwise Warn "<n> bases on USB: choose one." and emit `baseChoiceNeeded`.
- **Choose a Base dialog** (`mainwindow.cpp:535-555`): `QInputDialog::getItem`, title "Choose a Base".
  - Label is "Several bases are connected. Use:" when there are several, otherwise "Use:".
  - Items: `"ST-LINK <serial or 'serial unknown'>  (bus <b>, address <a>)"`. The current item is the saved serial.
- **chooseBase(i)** (lines 227-271):
  - With a serial: save it, set Ok "ST-LINK <serial>", then `baseReady`.
  - With an empty serial: Pending "Reading the ST-LINK serial", then run `-l st-link` and take the probe serials not claimed by any base. If exit code is 0, exactly one is unclaimed, and exactly one base has no serial, adopt and save it; Ok "ST-LINK <serial>".
  - Else, if this is the only base: Ok "ST-LINK (serial not readable; the only base)".
  - Else: Fail "Cannot read this base's ST-LINK serial, so it cannot be told from the others; unplug the other bases." and set `haveBase_` false.
- **baseReady** (lines 273-280): if auto-attach is on and Programmer is Ok, call `attach()`; otherwise Swd Pending "Press Attach to connect (this resets the tag)."
- **listBases** (lines 218-225), used by the menu:
  - Refused if a session is running, the CLI is busy, or an attach is in progress.
  - Otherwise `forgetTag()` (this detaches the tag), then re-enumerate.
- **cliSerial()** (lines 294-297): empty when exactly one ST-LINK is on USB, so `sn=` is omitted; otherwise the serial.

**attach()** (lines 318-364):
1. Ignored if a session is running, the CLI is busy, or an attach is in progress.
2. `forgetTag()`.
3. If Programmer is not Ok: Swd Pending "Waiting for STM32_Programmer_CLI." and stop.
4. If there is no base, run `checkBase`.
5. Re-enumerate and find the base by serial, or the only base. If it is not found: Base Warn "The base is not on USB. Plug it in." and start `basePoll_`.
6. Base Ok (text as above). Supply, Tag and TagState become Pending with empty text. Swd Pending "Connecting over SWD".
7. Run the CLI connect check. Then:
   - Failure: Swd Fail "<error>. Is a tag in the base, and powered?", then `checkSupply` and `checkFirmware`.
   - Success: Swd Ok `"0x%03x <deviceName>"` (lowercase hex, at least 3 digits, e.g. "0x435 STM32L43xxx/STM32L44xxx"), then `checkSupply` and `attachTag(10)`.

**attachTag(tries)** (lines 366-393):
- If `tag.Attach(base)` and `tag.GetTagInfo(info)` succeed:
  - Tag Ok "<board_desc>, <githash>"
  - `readStatus()`, `checkTagState()`, `detectTarget()`, start `statusPoll_`.
- Else, if tries > 1 and nothing is attached: retry after 200 ms (`attaching_` = true).
- Else: Detach; Tag Warn "No firmware answers: a blank board, or one in its bootloader. Choose the tag type."; TagState Pending.
- In every case it then emits `tagChanged` and runs `checkFirmware`.

**Supply** (lines 395-411), from the connect voltage V:
- V < 0: Pending.
- Device 0x435 and V > 3.6: Fail "<V> V: above the STM32L432's 3.6 V maximum. Lower the supply."
- V < 1.8: Warn "<V> V: low; programming may fail."
- Otherwise: Ok "<V> V". V is shown with 2 decimals.

**TagState** (lines 413-436):
- IDLE: Ok with the state name.
- ABORTED or FINISHED: Warn "<NAME>: holds a log, which programming erases. Download it with Tag Monitor first."
- TEST or sRESET: Pending "<NAME> in progress".
- Anything else: Warn "<NAME>: deployed. Programming stops it and erases its log."

**poll()** (400 ms; lines 438-466). It is skipped while a session runs or when nothing is attached.
- If `GetStatus` fails 5 times in a row:
  - `forgetTag`
  - Tag Warn "The tag stopped answering. Press Attach."
  - TagState Pending
  - Swd Pending "Press Attach to connect (this resets the tag)."
- Otherwise it computes the clock offset as `(status.millis − host ms since epoch) / 1000`. It redraws only if one of these changed: state, test_status, sectors_erased, or the offset by at least 0.05 s.

**nextTag** (lines 468-477):
- `forgetTag`
- Swd Pending "Swap the tag, then press Attach."
- Supply, Tag and TagState Pending
- `checkFirmware`

**Other actions**
- `stopTag` calls `Tag::Stop`.
- `eraseTag` calls `Tag::Erase`, only when the state is FINISHED or ABORTED. The UI first shows a warning box "Erase Tag", text "Erase the tag's log? Download it with Tag Monitor first; it cannot be recovered.", with buttons Cancel (default) and a destructive "Erase" (`mainwindow.cpp:223-233`).
- `syncClock` calls `Tag::SetRtc` and then `readStatus`. On failure: warning "Sync Clock" / "The tag did not accept the clock setting."

**Auto-attach**
- `setAutoAttach(on)` writes `resetsAcknowledged`. If on, a base is known and nothing is attached, it calls `attach()`.
- Automatic attaches happen from `baseReady` and from the programmer-check callback.
- The base poll does not attach by itself: it calls `checkBase`, then `chooseBase`, then `baseReady`, which attaches only if auto-attach is on.

**canProgram** (lines 703-723). Checks in order; the first failure gives the reason:
1. Session running: "Programming is in progress."
2. Attach in progress or CLI busy: "Connecting to the tag."
3. Programmer, Base, Swd and Record must each be Ok; otherwise the reason is that check's text.
4. Supply must not be Fail; otherwise its text.
5. Firmware must not be Fail or Pending, and the image must be usable; otherwise the Firmware text.

Tag and TagState do not block Program; Warn levels do not block it either.

---

## 3. Programming session (`Q/programsession.{h,cpp}`)

**Steps** (`programsession.h:72-83`): Stopping, Erasing, Programming, Reading, Connecting, PowerCycle, Testing, Finishing, Done.

**Constants** (`programsession.cpp:17-28`):
- `kStopTicks` = 30
- `kConnectTicks` = 6
- `kTestTicks` = 120
- IMU log page = 2048 bytes; sector = 4096 bytes
- The timer interval is 1000 ms.

`setStep` logs the step text with qInfo and emits `stepChanged`.

**`Station::program(label)`** (`station.cpp:725-737`):
1. `checkRecord`, then return unless `canProgram`.
2. Save `lastLabel`.
3. Snapshot the image and the voltage; stop `statusPoll_`.
4. `session.start(image, cliSerial(), reattach)`.
- `reattach` (lines 282-292): Detach, Available, then Attach the base whose serial matches, or the only base.
- The UI first shows the Program dialog (`mainwindow.cpp:598-698`):
  - Title "Program Tag". A QFormLayout with:
    - "Label": QLineEdit, placeholder "The label on the board, e.g. BitTag-017", empty at first.
    - "Tag": `"<target>  <STATE>  UUID <uuid>  <supply text>"`, or "No firmware answers (blank board); <supply text>".
    - "Image": `"<displayName>, commit <gitHash or first 8 of manifestCommit>\nSHA-256 <sha>"`.
    - "Erase": `plan.summary()`.
    - "Lost" and "Kept": "• "-bulleted lists.
  - Lost list:
    - if the tag answers: "The tag's log. Download it first with Tag Monitor."
    - always: "The tag's stored configuration."
    - calibrated tag (TagType COMPASSTAG or IMUTAG) with a no-layout image: "Its calibration constants: this image predates the flash layout record, so all of flash is erased."
    - calibrated tag with a plan that does not keep calibration: "Its calibration constants: this image keeps no calibration page."
  - Kept list: "Its calibration constants." if calibrated and kept, then "The RTC trim and the board UUID."
  - Amber word-wrapped warning label, one note per line, hidden when empty:
    - the target mismatch: "The image is for X, and the tag runs Y. Is it the right image?"
    - `qualificationWarning()`
    - "<label> is already recorded for UUID <list>." (unless the only such UUID is this tag's)
    - "This tag is recorded as <labels>." (when this UUID has labels that do not include the one typed)
  - Buttons: "Erase and Program" (AcceptRole, enabled only when the trimmed label is non-empty) and Cancel, which is set as the default.
  - If `canProgram` fails first: warning "Program Tag" with the reason.

**`start()`** (lines 70-93):
- If nothing is attached (a blank board), go straight to `runProgrammer`.
- Otherwise Stopping "Stopping the tag" and start the timer.

**tick()** (lines 252-417). Each tick polls `GetStatus` (`answered`, `state`).

- **Stopping**
  - Not answering: finish `"FAILED: the tag stopped answering"` / "The tag stopped answering before it was programmed."
  - ABORTED or FINISHED: call `Tag::Erase()`.
    - If it fails: `"FAILED: the tag refused the erase"` / "The tag refused to erase its log."
    - Otherwise reset the wait and go to Erasing "The tag is erasing its log".
  - Not IDLE: on the first tick, if the state is not TEST or sRESET, call `Tag::Stop()`. After more than 30 ticks: `"FAILED: the tag stayed <STATE>"` / "The tag stayed <STATE>. Stop and erase it in Tag Monitor, then program again."
  - IDLE with `finishing_` set: `SetRtc`, then finish with:
    - `MISMATCH` / "Programmed, but the tag reports commit <h>, which is not the image's. It is not running the image just written.", or
    - `OK` / "Programmed and tested: ALL_PASSED. The tag is erased and IDLE.", or
    - `TEST_FAILED` / "Programmed, but the self-test reported <X>."
  - IDLE otherwise: `runProgrammer()`.
- **Erasing**
  - sRESET: emit `eraseProgress(sectors_erased, total)`.
    - total = `erase_sectors_total_plus_one − 1` when that field is > 0.
    - Otherwise, from TagInfo: for IMUTAG, ceil(external_data_count × 2048 / 4096); else ceil(extflashsz / 4096).
  - ABORTED or FINISHED: keep waiting.
  - Not answering, or not IDLE: `"FAILED: no IDLE after the erase"` / "The tag did not return to IDLE after its erase (it is <STATE or 'not answering'>)."
  - IDLE: set the step to Stopping (no signal) and run `tick()` again immediately.
- **runProgrammer** (lines 198-218):
  1. Stop the timer and Detach.
  2. Programming "Programming <displayName>: <plan.summary()>"; set `programmed` = true.
  3. Run `cli.program(serial, elf, plan.eraseArgs())`.
  - Exit code ≠ 0: `"PROGRAM_FAILED: <parseConnect error>"` / "STM32_Programmer_CLI failed (<err>). Its output is in the log."
  - Exit code 0: Connecting "Connecting to the new image"; reset the wait and start the timer.
- **Connecting**
  - If attached, or `attach_()` succeeds: stop the timer and `beginTests()`.
  - Otherwise, after 6 ticks, stop the timer and go to PowerCycle with one of these texts:
    - retest: "The tag does not answer after its flash was read. Unplug the base, plug it in again, and press Retry."
    - mass erase: "Programmed, but the tag does not answer. A mass erase can leave it in its bootloader until it is powered off: unplug the base, plug it in again, and press Retry."
    - otherwise: "Programmed, but the tag does not answer. Unplug the base, plug it in again, and press Retry."
- **retryConnect** (only at PowerCycle): Connecting "Connecting to the new image"; reset the wait; start the timer.
- **beginTests** (lines 220-240):
  1. `GetTagInfo` gives the uuid and githash.
  2. Unless this is a retest, compare commits:
     - expected = `identity.gitHash`, or `manifestCommit` if that is empty
     - mismatch when expected is non-empty and is not the same commit as the tag's
     - "Same commit" means both are non-empty and one is a case-insensitive prefix of the other.
     - A mismatch is logged with qWarning.
  3. `SetRtc()`, then `Test(RUN_ALL)`; Testing "Running the self-tests".
- **Testing**
  - Answering and state TEST: after more than 120 ticks, `TEST_FAILED` / "The self-tests did not finish."
  - Not answering: `"FAILED: the tag stopped answering"` / "The tag stopped answering during its self-tests."
  - Otherwise: testStatus = `TestResult_Name`; `SetRtc()`.
    - Retest: finish as described in the Retest section below.
    - Programming: `SetRtc()`, then `GetConfig(cfg)` and `Start(cfg)`.
      - If either fails: `"FAILED: no start after testing"` / "The programmed tag would not start for its final erase."
      - Otherwise set `finishing_`; emit Finishing "Starting, stopping and erasing the tag once"; quietly set the step back to Stopping. The next tick stops the tag, erases it, waits for IDLE, then finishes.
- **cancel()** (lines 181-196):
  - At PowerCycle: `NO_ANSWER` / "Programmed, but the tag never answered. Power-cycle it and press Attach; if it still does not answer, program it again."
  - At Stopping or Erasing: `CANCELLED` / "Cancelled before programming."
  - Otherwise refused (returns false).
  - **Quirk:** in the final pass the step is Stopping or Erasing again, so Cancel is visible and accepted there. It records CANCELLED with "Cancelled before programming" even though the tag was already programmed.
- **finish** stops the timer, clears `running`, sets the result and message, emits Done with the message, then emits `finished`.

**Retest** (`Station::retest`, `station.cpp:739-770`; UI at `mainwindow.cpp:716-730`)
- UI: `QInputDialog::getText`, title "Retest Tag", label "Label of the tag to retest:", pre-filled with the last label recorded for this UUID.
- Image choice: if the tag's githash is the same commit as the chosen image's, use the chosen image. Otherwise use the first cached release (newest first) that has the tag's target and whose image is usable with the same commit.
- **`ProgramSession::retest`** (lines 95-127):
  - If nothing is attached and `attach_()` fails: `"FAILED: the tag does not answer"` / "The tag does not answer, so it cannot be tested."
  - No usable image: `beginTests` directly (flash NotChecked).
  - Status unreadable, or the tag not IDLE: Unread, "the tag was not IDLE, so its flash was not read", then `beginTests`.
  - Otherwise `readFlash`.
- **readFlash** (lines 129-170):
  1. Get the range from `imageFlashRange`. On error: Unread with that error.
  2. Without a temporary directory: Unread "no temporary directory for the read-back".
  3. The read-back file is `<tmp>/readback.bin`. Detach.
  4. Reading "Reading the tag's flash to compare with <displayName>".
  5. `cli.readMemory`:
     - Exit code ≠ 0, or the file cannot be opened: Unread "STM32_Programmer_CLI could not read it (<err>)".
     - Otherwise `compareImageFlash` gives Matches (empty note) or Differs (the note, logged with qWarning).
  6. Delete the file; Connecting "Connecting to the tag again"; start the timer.
- **Retest finish** (Testing step, lines 369-391):
  - Result: `MISMATCH` if the flash Differs, else `OK` if ALL_PASSED, else `TEST_FAILED`.
  - Message: "Self-tests: <X>." followed by one of:
    - " The flash matches <name>."
    - " The flash is not <name>: <note>"
    - " The flash was not compared: <note>."
    - " No downloaded image matches the tag's commit, so its flash was not compared."
  - No final erase.

**After the session** (`Station::writeRecord`, `station.cpp:772-825`)
1. Re-read TagInfo. If it answers: `readStatus`, `checkTagState`, restart `statusPoll_`.
2. If nothing was programmed and this is not a retest: no row. Emit `programFinished(empty, false, message)`. **CANCELLED and pre-programming FAILED results are therefore never written.**
3. A retest without Matches or Differs clears the image, so the row names no image.
4. Row fields:
   - `label`
   - `tag_type` = image target, else the chosen target
   - `uuid` = from the session, else from the TagInfo read before
   - `firmware_version` = release, "custom", or empty
   - `git_hash` = from the session, else the image's
   - `date` = UTC now
   - `test_status`, `result`, `image_sha256`
   - `image` = the file name
   - `erase` = "none", "below-calibration", "all", or empty for a retest
   - `voltage`, `probe_serial` = the serial, `host_version` = `QTPROGRAM_HOST_VERSION`
5. Append the row. If written, the OK count or the failed count goes up. If not, log with qCritical "The record was NOT written: … Row: …" and change the message to "<msg>\n\nThe record was NOT written: <err>. The row is in the log."

**STM32_Programmer_CLI invocations** (`Q/programmercli.cpp`)
- Connection prefix (lines 32-39):
  - `-c port=SWD mode=UR`
  - or `-c port=SWD sn=<serial> mode=UR`
- Calls:
  - version: `--version`
  - list probes: `-l st-link`
  - connect check: `-c port=SWD [sn=X] mode=UR`
  - program, with erase arguments placed before `-d` (line 237):
    - no erase: `<prefix> -d <abs elf> -q -g 0x08000000`
    - mass erase: `<prefix> -e all -d <abs elf> -q -g 0x08000000`
    - erase below calibration: `<prefix> -e [0 N] -d …`, where `[0` and `N]` are separate arguments
  - read memory: `<prefix> -u 0x%08x <size decimal> <abs file>` (lines 226-231)
- Running a call (lines 165-203):
  - Already running: callback `(-1, "STM32_Programmer_CLI is already running.")`.
  - No path: callback `(-1, "STM32_Programmer_CLI was not found.")`.
  - stdout and stderr are merged. The working directory is the tool's own directory. Output is decoded as local 8-bit and ANSI escapes are stripped.
  - A crash gives -1. Failure to start appends "Could not start <path>." and gives -1.
- Parsers (lines 43-97); all regexes are applied per line after ANSI stripping:
  - ANSI: `\x1b\[[0-9;]*m`
  - version: `STM32CubeProgrammer v(\d+(?:\.\d+)+)`
  - probes: `^\s*ST-LINK SN\s*:\s*(\S+)`, and `^\s*ST-LINK FW\s*:\s*(\S+)` (assigned to the last probe)
  - connect:
    - `^\s*Device ID\s*:\s*0x([0-9A-Fa-f]+)`
    - `^\s*Device name\s*:\s*(.+?)\s*$`
    - `^\s*Voltage\s*:\s*([0-9.]+)\s*V`
    - the first `^\s*Error:\s*(.+?)\s*$`
  - The connect is ok when exit code is 0 and the device ID is non-zero. Otherwise the error defaults to "the programmer reported no device" (exit 0) or "STM32_Programmer_CLI exited with <n>".
- `findProgrammer` (lines 99-140), in order:
  1. the saved path, if it is an executable file
  2. env `STM32CUBEPROGRAMMER`, then `STM32_PRG_PATH`
  3. the PATH
  4. platform roots:
     - macOS: each of {/Applications, ~/Applications, ~/Software} combined with each of {STMicroelectronics/STM32Cube/STM32CubeProgrammer, STMicroelectronics/STM32CubeProgrammer, STM32CubeProgrammer, .}
     - Windows: C:/Program Files[ (x86)]/STMicroelectronics/STM32Cube/STM32CubeProgrammer
     - Linux: ~/STMicroelectronics/STM32Cube/STM32CubeProgrammer, ~/Software/STM32CubeProgrammer, /opt/st/stm32cubeprog, /usr/local/STMicroelectronics/STM32Cube/STM32CubeProgrammer
- `programmerInDirectory` (lines 142-161) accepts the tool itself, or looks under: `.`, `bin`, `Contents/MacOs/bin`, `Contents/Resources/bin`, `STM32CubeProgrammer.app/Contents/{MacOs,Resources}/bin`. The tool name is `STM32_Programmer_CLI` (with `.exe` on Windows).

---

## 4. flashplan (`Q/flashplan.cpp`), the exact logic

**Symbols read**, through a minimal ELF32 little-endian SHT_SYMTAB walk (lines 43-77):
`__tag_flash_page_size__`, `__calibration_start__`, `__calibration_end__`, `__flash0_base__`, `__flash0_end__`.

**ELF validation**
- Not "\x7fELF", or EI_CLASS ≠ 1, or EI_DATA ≠ 1, or file shorter than 0x34 bytes: "not a 32-bit little-endian ELF file".
- shentsize < 40, or the section table runs past the end of the file: "truncated ELF section table".
- Symtab entries with entsize < 16 are skipped.

**planFlash(path)** (lines 112-165):
```
cannot open             -> Invalid, "Cannot read <path>"
ELF error               -> Invalid, "<path> is <err>; qtprogram needs the firmware's .elf, whose symbols say what to erase."
no __tag_flash_page_size__:
   size = flash0_end - flash0_base (missing symbols count as 0)
   size == 256K -> All, noLayout=true, pageSize 2048
   size == 1M   -> All, noLayout=true, pageSize 4096
   else -> Invalid, "<path> does not say which flash layout it was built for (no __tag_flash_page_size__), nor for which MCU; rebuild it from a current tree."
pageSize = symbol value
  4096 -> None
  2048 -> start = calibration_start (default 0); end = calibration_end (default start)
          if start > 0x08000000 && end > start: BelowCalibration, lastErasedPage = (start-0x08000000)/2048 - 1
          else All
  other -> Invalid, "<path> was linked for <n>-byte flash pages, which no tag in this tree uses."
```

**eraseArgs** (lines 81-91):
- BelowCalibration: `{"-e", "[0", "<N>]"}`
- All: `{"-e", "all"}`
- otherwise: empty

**summary** (lines 93-110):
- None: "Program only (STM32U375: every saved region is pinned and kept)"
- BelowCalibration: "Erase flash pages 0-<N> and program, keeping the calibration page"
- All with noLayout: "Mass erase and program (<STM32U375 if page 4096, else STM32L432> image built before images recorded their flash layout)"
- All otherwise: "Mass erase and program (STM32L432 with no calibration page)"
- Invalid: the error text

**Derived properties**
- `keepsCalibration` = None or BelowCalibration
- `keepsConfiguration` = None

Note: `Q/README.md:43` says images without the page-size symbol are refused. The code mass-erases them when the flash size is known.

---

## 5. Releases and images (`Q/firmwarecatalog.cpp`, `Q/firmwareimage.cpp`)

**Listing**
- `GET https://api.github.com/repos/tag-designs/software/releases?per_page=100` with headers `Accept: application/vnd.github+json` and `User-Agent: qtprogram`. No token.
- Parsing (lines 109-140):
  - Drafts are skipped, as are tags not matching `^fw-v\d+(\.\d+)*$`.
  - `published_at`, `prerelease` and `html_url` (the page URL) are read.
  - The archive is the asset named exactly `tag-firmware-<tag>.tar.gz` (its `browser_download_url`). Releases without that asset are dropped.
  - `qualified` = targets taken from asset names matching `^qualification-([A-Za-z0-9_]+)(-\d{8})?\.tar\.gz$`.
  - `listed` = true.
  - Sorted by `QVersionNumber(tag.mid(4))`, descending.
- On HTTP error: 403 or 429 gives "GitHub refused the listing (rate limit); try again later."; otherwise the error string. Emits `refreshFailed`, which the window logs as qWarning "Could not list firmware releases: …".
- On success the listing is written to `<cache>/releases.json` as an array of `{tag, published, prerelease, archive, page, qualified[]}`.
- `setReleases` (lines 142-165) adds every cached directory not in the listing, marks the `cached` flags and sorts.
- The constructor loads `releases.json`. In that file `listed` is true only if the `qualified` key is present.

**Cache**
- Location: `QStandardPaths::AppDataLocation/firmware` (`station.cpp:74-75`).
- Layout:
  - `<cache>/<tag>/.verified`
  - `<cache>/<tag>/firmware/<T>/<T>.elf`
  - `<cache>/<tag>/firmware/<T>/<T>-build-manifest.json`
- `isCached` = the tag matches the regex and `.verified` exists.
- `targets(tag)` = sorted subdirectories of `firmware/` that contain `<T>.elf`.

**Fetch** (lines 209-322)
1. Already cached: emit `fetched` at once.
2. Already fetching: `fetchFailed` "Another release is downloading.". No archive URL: "<tag> has no firmware archive."
3. Staging directory `<cache>/<tag>.partial`, emptied first. Download (User-Agent qtprogram) to `archive.tar.gz`, with a qInfo "Downloading <url>".
4. `tar -tzf archive`. Keep members, with a leading "./" stripped, that match `^firmware/([A-Za-z0-9_-]+)/\1(\.elf|-build-manifest\.json)$`.
5. `tar -xzf archive -C staging <members…>`, then delete the archive.
6. Load and verify every image with `loadFirmwareImage(…, tag)`. One bad image fails the whole release.
7. Rename staging to `<cache>/<tag>` (after removing any old copy) and write `.verified`.
8. Failure messages:
   - "The archive could not be read: …"
   - "The archive holds no firmware images."
   - "Extraction failed: …"
   - "Cannot write <archive>."
   - "Cannot install the release in <dir>."
   - "Could not run tar."
   On failure the staging directory is deleted and qWarning "Fetching <tag> failed: <why>" is logged.

**loadFirmwareImage(elf, release)** (`firmwareimage.cpp:141-175`)
- Not a file: "<path> does not exist."
- Then SHA-256 (lowercase hex); if it cannot be read: "Cannot read <path>."
- `planFlash`; if Invalid, its error.
- `readImageIdentity` (lines 97-139):
  - looks for "TGID" at 0x08000000 + 0x1A0, then + 0x240
  - the record size is bytes 6-7 (little-endian)
  - parsed with tagcore `ParseIdentityRecord`
  - fields: target, board, board_desc, git_hash, git_sha, source_path, tag_type
- Manifest = `<dir>/<completeBaseName>-build-manifest.json`. If it exists, verify (lines 40-76):
  - `artifacts[<elf name>]` missing: "<manifest> records no hash for <name>."
  - sha256 differs: "<name> does not match its manifest (recorded X, actual Y). This is not the image that was archived; do not program it."
  - bytes differ: "<name> is <n> bytes; its manifest says <m>."
  - Otherwise `manifestVerified`, and `manifestCommit = source.commit`.
  - `source.dirty` gives the warning "Built from a tree with uncommitted changes, so its commit does not describe its sources."
- A release image without a manifest: "The release has no <manifest> beside <elf>, so the image cannot be checked."
- `displayName` = `"<identity.target or file name> (<release or 'custom'>)"`.

**Choosing firmware** (`station.cpp:516-582`)
- `chooseRelease`: sets `firmwareRelease` and removes `firmwareFile`, then `loadImage`.
- `chooseFile`: sets `firmwareFile` and removes `firmwareRelease`. `recentFiles` gets the path moved to the front, capped at 6. Then `loadImage`.
- File dialog for "Other ELF file...": title "Firmware image to program"; starts in the last file's directory or home; filter "Firmware ELF (*.elf);;All Files (*)".
- `loadImage`:
  - A file: load it; if it has an identity, target = identity.target (not saved).
  - A release not cached: fetch it if it has an archive URL.
  - A release cached with the target among its targets: load `imagePath`.
  - Then `checkFirmware` and emit `firmwareChanged`.
- Catalog signals (lines 84-103): `releasesChanged` reloads the image if a release is chosen, no image is loaded and nothing is fetching. `fetched` and `fetchFailed` apply only to the chosen release.

**Tag type**
- `detectTarget` (lines 554-563): target = the last path component of `TagInfo.source_path`, but only if a release is chosen and that target is in its list. Sets `targetDetected` and saves `target`.
- `chooseTarget` saves `target` and clears `targetDetected`.
- The release is not chosen automatically. "Latest qualified" is only advice in the manual (line 63).

**checkFirmware** (lines 584-641), in order:
1. Nothing chosen: Fail "Choose the firmware."
2. Release not cached, then the first that applies:
   - a fetch error: Fail "<rel> could not be downloaded: <why>"
   - fetching: Pending "Downloading <rel>"
   - refreshing: Pending "Looking up <rel> on GitHub"
   - a refresh error: Fail "<rel> is not downloaded, and GitHub cannot be reached: <err>"
   - otherwise: Fail "<rel> is not a published release."
3. Release chosen but the target is not in its list: Fail "Choose the tag type." if the target is empty, else "<rel> has no image for <target>."
4. Image not usable: Fail with the image error.
5. MCU mismatch (device ID known, and the image page size is 4096 but the device is not 0x454, or the reverse): Fail "This image is for an <A>, and the tag is an <B>."
6. Tag runs a different target: Warn "The image is for X, and the tag runs Y."
7. Image warning: Warn with it.
8. `qualificationWarning` (lines 643-657):
   - a file: "<name> is a local file, not a qualified release."
   - release not listed: "Cannot tell whether <rel> is qualified for <T>: GitHub has not been reached."
   - not qualified: "<rel> is not qualified for <T>. See the Release Notes."
   Any of these is a Warn.
9. Otherwise Ok: "<displayName>, verified against its manifest" or "<displayName>, no manifest to check".

---

## 6. Record, settings, command line, Tag API

**Record CSV** (`Q/recordlog.cpp`)
- Header: `label,tag_type,uuid,firmware_version,git_hash,date,test_status,result,image_sha256,image,erase,voltage_v,probe_serial,host_version`.
- `date` is ISO 8601 UTC with a Z suffix. `voltage` has 2 decimals, or is empty.
- Quoting follows RFC 4180: a field is quoted only if it contains a comma, quote, CR or LF.
- Rows are appended and flushed.
- `open` requires the first 7 header columns.
- Errors:
  - "Cannot read X." / "X does not exist."
  - "X is not a Tag Programmer record: its first line should be the header \"…\"."
  - "X already exists."
  - "Cannot create X: <err>."
  - "No record file is open."
  - "Cannot write X (<err>). Close it in any other program."
  - append: "Cannot write X (<err>)."
- Record check (`station.cpp:686-699`):
  - Fail "No record file. Create or choose one."
  - Fail with the `writable` error
  - Ok "<name> (1 row)" or "<name> (<n> rows)"
- Default path: `Documents/Tag Programmer/programmed-tags.csv`. Its directory is created on create.
- **Record File dialog** (`mainwindow.cpp:557-596`): question box, title "Record File", text "Tag Programmer records every tag it programs in a CSV file."
  - If the default exists: informative "Use the record at <p>, or choose another?", first button "Use It".
  - If not: "Create one at <p>, or choose another location?", first button "Create".
  - Other buttons: "Open Existing...", "Create Elsewhere...", Cancel. The first button is the default.
  - "Open Existing..." opens a file dialog "Record file" with filter "CSV (*.csv)".
  - "Create Elsewhere..." opens a save dialog "New record file" (filter "CSV (*.csv)"). If the chosen file exists it is opened instead of created.
  - Errors appear in a warning box titled "Record File".

**QSettings keys** (`station.cpp:19-28`)
- `programmer`
- `firmwareRelease`
- `firmwareFile`; the legacy `lastImage` is used as its fallback
- `recentFiles` (string list)
- `target`
- `recordPath`
- `baseSerial`
- `resetsAcknowledged`: the auto-attach boolean; whether it exists at all decides the first-run question
- `lastLabel`

A saved release overrides a saved file (lines 105-111).

**Command line** (`Q/main.cpp:146-227`)
- `--flash-plan <elf>...`: prints `"<name> | <eraseArgs joined> | <summary>"` per file and exits 0. No GUI.
- `--verify <elf|dir>...`: a directory resolves to `<dir>/<dirname>.elf`, or to each `<sub>/<sub>.elf`. Per image it prints:
  ```
  <path>
    target    <target or 'no identity record'> (<boardDesc>, TagType <n>)
    commit    <gitHash>
    sha256    <sha>
    manifest  <verified|none|REFUSED>
    erase     <summary>
  ```
  plus optional `  warning   …` and `  REFUSED   <error>` lines. Exits 1 if any image is refused.
- `--capture-screenshots [dir]`: runs after 100 ms. Default dir is `<source>/host/docs/src/images`.
  1. Wait up to 60 s while `busy()`, then 1 s more.
  2. Grab the window frame to `qtprogram-station.png`.
  3. If the image is usable, open the Program dialog with label "BitTag-017", save it after 1 s as `qtprogram-program.png`, and reject it.
  4. Otherwise print "No usable firmware chosen; the Program dialog was not captured." to stderr.
  5. "Saved screenshot"/"Could not save screenshot" lines go to stderr. Exit 0 or 1.
- `--bench-program <label>`: runs after 100 ms.
  1. Wait while busy.
  2. Print each check as `"%-10s %-5s %s"` with level ok, warn, FAIL or "-".
  3. If it cannot program: print "Cannot program: <why>" and exit 2.
  4. Print each step as `"[HH:mm:ss] <text>"`. At PowerCycle, call `retryConnect` every 5 s, up to 36 times, then `cancel`.
  5. At the end: `"Recorded: yes|NO\n<csv fields>"`. Exit 0 if the row was written and the result is OK, otherwise 1.

**tagcore Tag API usage**
- `Available(vector<UsbDev>&)`: `checkBase`, `listBases`, `attach`, `reattach`. `UsbDev` holds vid, pid, bus, address and serial.
- `Attach(UsbDev)`: `attachTag`, `reattach`.
- `Detach()`: destructor, `forgetTag`, `reattach`, failed attach, `runProgrammer`, `readFlash`.
- `IsAttached()`.
- `GetTagInfo(TagInfo&)`: attach, `writeRecord`, `beginTests`, the erase-total fallback. Fields used: uuid, githash, board_desc, source_path, tag_type, extflashsz.
- `GetStatus(Status&)`: the 400 ms poll, `readStatus`, the session tick. Fields used: state, test_status, sectors_erased, millis, erase_sectors_total_plus_one, external_data_count.
- `SetRtc()`: Sync; `beginTests`; after the tests; before Start; at the final finish.
- `Stop()`: the Stop button; the session's Stopping step.
- `Erase()`: the Erase button; the session for ABORTED or FINISHED.
- `Test(RUN_ALL)`.
- `GetConfig(Config&)` and `Start(Config&)`: the final pass.
- Enums and helpers: `TagState_Name`, `TestResult_Name`; IDLE, ABORTED, FINISHED, TEST, sRESET, STATE_UNSPECIFIED; COMPASSTAG, IMUTAG.

---

## 7. Other dialog texts
- Every dialog and message is quoted where it is described above.
- `Q/test/qtprogram_checks.cpp` holds captured CLI output for a reimplementation's parser tests:
  - connect output: lines 54-69
  - probe list: lines 72-80
  - failed connect: "Error: No STM32 target found!"

## Discrepancies to decide on when porting
1. `outputLine` is never connected, so the programmer's output never reaches the log pane, even though messages and the manual say it does.
2. The log level comment in `main.cpp` is stale: the effective level is INFO, not ERROR.
3. Cancel during the final erase records CANCELLED with "Cancelled before programming."
4. CANCELLED and pre-programming FAILED results are never written to the CSV.
5. `README.md:43` describes a refusal rule for images without the page-size symbol that the code does not apply.
6. The station screenshot predates the Clock/Sync row.
7. The `qtprogram-main-window.png` referenced by `documentation-guidelines.md` is missing.
8. Log save opens the file without truncating it.
9. Choose Base... detaches the tag even if the dialog is then cancelled.
