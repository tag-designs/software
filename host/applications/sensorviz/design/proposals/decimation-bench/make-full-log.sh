#!/bin/sh
# Build a synthetic worst-case IMUTag SQLite log: the committed sensorviz IMUTag
# fixture with ImuAccel and ImuGyro refilled to a full 2 Gbit NAND
# (131,072 pages x 150 samples = 19,660,800 rows each, 400 Hz, one segment).
# Other tables keep the fixture's small contents. Needs ~2.2 GB of disk and
# the sqlite3 shell; takes about a minute.
#
#   make-full-log.sh <output.db3>
set -eu
out=${1:?usage: make-full-log.sh <output.db3>}
here=$(cd "$(dirname "$0")" && pwd)
cp "$here/../../../../../docs/fixtures/sensorviz/imutag.db3" "$out"
sqlite3 "$out" <<'SQL'
PRAGMA journal_mode=OFF;
PRAGMA synchronous=OFF;
DELETE FROM ImuAccel;
DELETE FROM ImuGyro;
-- Values sit on the int16 x scale grid the writer produces (0.061 mg/LSB,
-- 0.0175 dps/LSB), so REAL columns are stored as full 8-byte doubles.
WITH RECURSIVE n(i) AS (SELECT 0 UNION ALL SELECT i + 1 FROM n WHERE i < 19660799)
INSERT INTO ImuAccel
SELECT 0, i, i * 2500, i * 2500,
       round(1000 * sin(i / 4000.0) + (abs(random()) % 200) - 100) * 0.061,
       round(500 * cos(i / 9000.0) + (abs(random()) % 200) - 100) * 0.061,
       round(16000 + (abs(random()) % 400) - 200) * 0.061
FROM n;
INSERT INTO ImuGyro
SELECT SegmentId, SampleIndex, RawElapsedUs, ElapsedUs,
       round(ax * 3.1 + (abs(random()) % 50) - 25) * 0.0175,
       round(ay * 2.3) * 0.0175,
       round((abs(random()) % 300) - 150) * 0.0175
FROM ImuAccel ORDER BY rowid;
VACUUM;
SQL
