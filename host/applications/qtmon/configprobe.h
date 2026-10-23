/**
 * @file    configprobe.h
 * @brief   Headless runner that feeds configuration cases through ConfigTab.
 *
 * @details `qtmonitor --config-probe <cases> --config-probe-out <dir>` runs
 *          each case through a real ConfigTab, with no tag attached, and
 *          records what Start would send, a layout-independent fingerprint of
 *          the visible controls, and any dialogs raised. Results from two
 *          builds are compared by `host/applications/qtmon/test/compare.py` to
 *          show that a change to the configuration screen leaves its output
 *          unchanged. The case format, result format and procedure are in
 *          host/applications/qtmon/test/README.md.
 */

#ifndef CONFIGPROBE_H
#define CONFIGPROBE_H

#include <QString>

/**
 * @brief   Runs every case file and writes one result file for each.
 *
 * @param[in] cases_path  A case file (`*.json`) or a directory of them.
 * @param[in] out_dir     Directory for result files, created if missing. Each
 *                        result file has its case file's name.
 *
 * @return  Process exit status: 0 when every case file was read and every
 *          result written; 1 otherwise, with the reason on stderr. A case whose
 *          configuration cannot be applied is a result, not a failure.
 *
 * @pre     A QApplication exists. Run under `QT_QPA_PLATFORM=offscreen`, `TZ=UTC`
 *          and the C locale for output that does not depend on the machine.
 * @warning Fixes ConfigClock for each case and clears it on return.
 */
int runConfigProbe(const QString &cases_path, const QString &out_dir);

#endif // CONFIGPROBE_H
