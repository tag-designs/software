/**
 * @file    preference_check.cc
 * @brief   Offline assertion checks for sensorviz::PreferenceStore and color parsing, without Qt.
 *
 * @details Checks the preference file the store writes against text Qt's
 *          `QJsonDocument::Indented` wrote for the same preferences, reading
 *          it back, the store's entry rules, each refusal and its message,
 *          and colorFromString() on values Qt's `QColor` gave. The expected
 *          values were taken from Qt when the store replaced the Qt code.
 *
 *          Exits 0 when every check passes, 1 otherwise; each failure is
 *          printed with its line.
 */
#include "preference_store.h"

#include <cstdio>
#include <string>

namespace
{

using sensorviz::AxisSide;
using sensorviz::PreferenceStore;
using sensorviz::Rgb;
using sensorviz::TagPreferences;

int g_failures = 0; ///< Checks failed so far.
int g_checks = 0;   ///< Checks run so far.

/// Records one check; prints the failing expression and line.
#define CHECK(condition)                                                              \
    do {                                                                              \
        g_checks++;                                                                   \
        if (!(condition)) {                                                           \
            g_failures++;                                                             \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        }                                                                             \
    } while (0)

/// Two tags' preferences, one with every kind of override.
PreferenceStore sampleStore()
{
    TagPreferences imu;
    imu.tagType = "IMUTAG";
    imu.hasVisibleStreamOverride = true;
    imu.visibleStreamIds = {"imu_pressure", "imu_ax"};
    imu.streamColors["imu_ax"] = Rgb{255, 0, 0};
    imu.streamColors["altitude"] = Rgb{70, 130, 180};
    imu.axisSides["imu_pressure"] = AxisSide::Right;
    imu.hasBreakLinesAtGapsOverride = true;
    imu.breakLinesAtGaps = false;

    TagPreferences compass;
    compass.tagType = "COMPASSTAG";
    compass.hasVisibleStreamOverride = true; // an empty visible set is an override

    PreferenceStore store;
    store.remember(imu);
    store.remember(compass);
    return store;
}

/// What Qt wrote for sampleStore().
const char *kSampleFile = R"({
    "tags": {
        "COMPASSTAG": {
            "visible_streams": [
            ]
        },
        "IMUTAG": {
            "axis_sides": {
                "imu_pressure": "right"
            },
            "break_lines_at_gaps": false,
            "colors": {
                "altitude": "#4682b4",
                "imu_ax": "#ff0000"
            },
            "visible_streams": [
                "imu_ax",
                "imu_pressure"
            ]
        }
    },
    "version": 1
}
)";

/// Writing, the entry rules, and reading back.
void checkRoundTrip()
{
    PreferenceStore store = sampleStore();
    CHECK(store.save() == kSampleFile);

    PreferenceStore empty;
    CHECK(empty.save() == "{\n    \"tags\": {\n    },\n    \"version\": 1\n}\n");

    // Keys and strings are escaped and ordered as Qt did: characters above
    // U+FFFF (UTF-16 surrogates) sort before U+E000.
    TagPreferences odd;
    odd.tagType = "q\"\\\x01\t/\x7f";
    odd.axisSides["\xee\x80\x80"] = AxisSide::Left;      // U+E000
    odd.axisSides["\xf0\x9f\x98\x80"] = AxisSide::Left;  // U+1F600
    PreferenceStore escaped;
    escaped.remember(odd);
    CHECK(escaped.save()
          == "{\n    \"tags\": {\n        \"q\\\"\\\\\\u0001\\t/\x7f\": {\n"
             "            \"axis_sides\": {\n"
             "                \"\xf0\x9f\x98\x80\": \"left\",\n"
             "                \"\xee\x80\x80\": \"left\"\n"
             "            }\n        }\n    },\n    \"version\": 1\n}\n");

    std::string error;
    PreferenceStore loaded;
    CHECK(loaded.load(kSampleFile, "f.json", error));
    CHECK(loaded.save() == kSampleFile);
    const TagPreferences imu = loaded.get("IMUTAG");
    CHECK(imu.tagType == "IMUTAG");
    CHECK(imu.hasBreakLinesAtGapsOverride && !imu.breakLinesAtGaps);
    CHECK(imu.axisSides.at("imu_pressure") == AxisSide::Right);
    CHECK(loaded.get("COMPASSTAG").hasVisibleStreamOverride);
    CHECK(loaded.get("COMPASSTAG").visibleStreamIds.empty());
    CHECK(!loaded.get("BITTAG").hasOverrides());

    // No overrides: not stored, and remembering it removes an entry.
    TagPreferences none;
    none.tagType = "IMUTAG";
    loaded.remember(none);
    CHECK(loaded.entries().size() == 1);
    loaded.forget("COMPASSTAG");
    CHECK(loaded.entries().empty());

    // Lenient values Qt read: no version, trimmed sides in any case, names.
    CHECK(loaded.load(R"({"tags": {"T": {"axis_sides": {"a": " Right "},
                         "colors": {"a": "Steel Blue"}}, "E": {"colors": {}}}, "x": 1})",
                      "f.json", error));
    CHECK(loaded.entries().size() == 1);
    CHECK(loaded.get("T").axisSides.at("a") == AxisSide::Right);
    CHECK(loaded.get("T").streamColors.at("a") == (Rgb{70, 130, 180}));
}

/// Each refusal, its message, and the store left as it was.
void checkRefusals()
{
    const struct
    {
        const char *json;    ///< The file.
        const char *message; ///< The error, or its start for parse errors.
    } cases[] = {
        {"[1]", "Preference file f.json must contain a JSON object"},
        {R"({"version": 2, "tags": {}})", "Unsupported sensorViz preferences version in f.json"},
        {R"({"version": "1", "tags": {}})", "Unsupported sensorViz preferences version in f.json"},
        {R"({"version": 1})", "Preference file f.json must contain a tags object"},
        {R"({"tags": {"T": 1}})", "Preferences for T must be a JSON object"},
        {R"({"tags": {"T": {"visible_streams": {}}}})", "visible_streams for T must be an array"},
        {R"({"tags": {"T": {"visible_streams": [1]}}})", "visible_streams for T must contain only strings"},
        {R"({"tags": {"T": {"colors": []}}})", "colors for T must be an object"},
        {R"({"tags": {"T": {"colors": {"a": 1}}}})", "colors.a for T must be a color string"},
        {R"({"tags": {"T": {"colors": {"a": "#12345"}}}})", "colors.a for T is not a valid color"},
        {R"({"tags": {"T": {"axis_sides": 1}}})", "axis_sides for T must be an object"},
        {R"({"tags": {"T": {"axis_sides": {"a": "up"}}}})", "axis_sides.a for T must be left or right"},
        {R"({"tags": {"T": {"axis_sides": {"a": 0}}}})", "axis_sides.a for T must be left or right"},
        {R"({"tags": {"T": {"break_lines_at_gaps": 0}}})", "break_lines_at_gaps for T must be true or false"},
        // Entries are checked in key order; the first fault is reported.
        {R"({"tags": {"B": 1, "A": 2}})", "Preferences for A must be a JSON object"},
        {"{", "Could not parse f.json: "},
    };
    for (const auto &c : cases) {
        PreferenceStore store = sampleStore();
        std::string error;
        CHECK(!store.load(c.json, "f.json", error));
        CHECK(error.rfind(c.message, 0) == 0);
        CHECK(store.save() == kSampleFile);
    }
}

/// colorFromString() against what QColor gave for the same text.
void checkColors()
{
    const struct
    {
        const char *text; ///< The color text.
        bool valid;       ///< Whether QColor accepted it.
        Rgb rgb;          ///< QColor's red, green and blue.
    } cases[] = {
        {"#abc", true, {170, 187, 204}},
        {"#ABCDEF", true, {171, 205, 239}},
        {"#80ff0000", true, {255, 0, 0}},
        {"#b207dABDc", true, {177, 125, 189}},      // 12-bit channels, Qt's rounding
        {"#e0DeFC77F", true, {224, 239, 119}},
        {"#88Cb3477db1e", true, {136, 52, 218}},    // 16-bit channels
        {"#e3B455D563Db", true, {227, 85, 99}},
        {"STEELBLUE", true, {70, 130, 180}},
        {"Steel\tBlue", true, {70, 130, 180}},
        {"red ", true, {255, 0, 0}},
        {"transparent", true, {0, 0, 0}},
        {"yellowgreen", true, {154, 205, 50}},
        {" #abc", false, {}},
        {"steel\nblue", false, {}},
        {"#ab", false, {}},
        {"#abcd", false, {}},
        {"#gggggg", false, {}},
        {"", false, {}},
    };
    for (const auto &c : cases) {
        Rgb color{1, 2, 3};
        const bool valid = sensorviz::colorFromString(c.text, color);
        CHECK(valid == c.valid);
        CHECK(valid ? color == c.rgb : color == (Rgb{1, 2, 3}));
    }
    CHECK(sensorviz::colorName(Rgb{0, 10, 255}) == "#000aff");
}

} // namespace

/// Runs every check; exits 0 when all pass, 1 otherwise.
int main()
{
    checkRoundTrip();
    checkRefusals();
    checkColors();
    std::printf("preference_check: %d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
