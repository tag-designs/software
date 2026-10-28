/**
 * @file    preference_store.h
 * @brief   Qt-free per-tag display preferences and the sensorViz preference file format.
 *
 * @details Part of `sensorviz_core`. A PreferenceStore holds one
 *          TagPreferences per tag type, for the life of the application, and
 *          reads and writes them as the preference file sensorViz has always
 *          used:
 *
 *          @code{.json}
 *          {
 *              "tags": {
 *                  "IMUTAG": {
 *                      "axis_sides": { "imu_ax": "right" },
 *                      "break_lines_at_gaps": false,
 *                      "colors": { "imu_ax": "#ff0000" },
 *                      "visible_streams": [ "imu_ax", "imu_pressure" ]
 *                  }
 *              },
 *              "version": 1
 *          }
 *          @endcode
 *
 *          Every key is optional within a tag entry; only overrides of the
 *          defaults are written. save() produces exactly the bytes Qt's
 *          `QJsonDocument::Indented` wrote, and load() accepts what Qt's
 *          reader accepted, colors included, so files written by either
 *          version read the same in the other. Choosing and reading the file
 *          stays in the front end.
 *
 *          Malformed JSON is where the two readers part: this one accepts
 *          trailing commas, which Qt refused, and refuses duplicate keys,
 *          unknown escapes such as `\,` and numbers such as `1.`, which Qt
 *          accepted. sensorViz writes none of these.
 *
 * @see     view_model.h (TagPreferences, ViewModel::capturePreferences(),
 *          ViewModel::applyPreferences())
 */
#ifndef SENSORVIZ_CORE_PREFERENCE_STORE_H
#define SENSORVIZ_CORE_PREFERENCE_STORE_H

#include <map>
#include <string>

#include "stream_meta.h"
#include "view_model.h"

namespace sensorviz {

/**
 * @brief   Parses a color as `QColor(QString)` does.
 *
 * @details Accepts `#rgb`, `#rrggbb`, `#aarrggbb` (alpha ignored),
 *          `#rrrgggbbb` and `#rrrrggggbbbb`, with hex digits in either case;
 *          or one of the 148 SVG color names, including `transparent` (black
 *          here, alpha being ignored), matched ignoring ASCII case and
 *          any spaces or tabs in the name. Wider channels are rounded
 *          to eight bits as Qt rounds them, through 16 bits.
 *
 * @param[in]  text   The color text.
 * @param[out] color  The color; unchanged on failure.
 * @return  false for anything else, including whitespace before a `#`.
 */
bool colorFromString(const std::string &text, Rgb &color);

/**
 * @brief   The color as `#rrggbb` in lower case, as `QColor::name()` writes it.
 * @param[in] color  The color.
 * @return  Seven characters.
 */
std::string colorName(Rgb color);

/**
 * @class   PreferenceStore
 * @brief   Per-tag sparse display overrides, and their JSON file.
 *
 * @details Holds at most one TagPreferences per tag type, and never one
 *          without an override (TagPreferences::hasOverrides()). Not
 *          thread-safe; use from one thread.
 */
class PreferenceStore
{
public:
    /// The preferences for @p tagType; empty (no overrides) when none are stored.
    TagPreferences get(const std::string &tagType) const;

    /**
     * @brief   Stores @p preferences under TagPreferences::tagType, or removes
     *          that entry when they hold no override.
     * @param[in] preferences  The tag's preferences, normally from
     *                         ViewModel::capturePreferences().
     */
    void remember(const TagPreferences &preferences);

    /// Removes @p tagType's entry, so the tag returns to the defaults.
    void forget(const std::string &tagType);

    /// Every stored entry, by tag type in byte order.
    const std::map<std::string, TagPreferences> &entries() const { return byTag_; }

    /**
     * @brief   Replaces every entry with those in a preference file.
     *
     * @details Unknown keys are ignored, at the top level and within a tag
     *          entry; unknown stream ids are kept, since a file may be loaded
     *          before the matching log. Entries without an override are
     *          dropped. A `version` other than 1 is refused; a file without
     *          one is read as version 1.
     *
     * @param[in]  json    The file's contents, UTF-8.
     * @param[in]  source  How to name the file in error messages, normally its path.
     * @param[out] error   Why the file was refused; set only on failure.
     *
     * @return  true on success. false, leaving the store unchanged, when the
     *          text is not a JSON object, the version is unsupported, `tags`
     *          is not an object, or any tag entry is malformed: a
     *          `visible_streams` that is not an array of strings, `colors`
     *          values that are not color strings colorFromString() accepts,
     *          `axis_sides` values other than `left` or `right` (ignoring
     *          case and surrounding whitespace), or a `break_lines_at_gaps`
     *          that is not a boolean. Entries are checked in key order and
     *          the first fault is reported.
     */
    bool load(const std::string &json, const std::string &source, std::string &error);

    /**
     * @brief   The preference file for every stored entry.
     * @return  The JSON text, byte for byte what Qt's `QJsonDocument::Indented`
     *          wrote: four-space indents, keys in order, a final newline.
     */
    std::string save() const;

private:
    std::map<std::string, TagPreferences> byTag_; ///< Entries with an override, by tag type.
};

} // namespace sensorviz

#endif
