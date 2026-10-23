/**
 * @file    configshape.h
 * @brief   Which Config fields a tag type supports, as dotted field paths.
 *
 * @details proto3 does not record whether a zero-valued scalar was set, so a
 *          decoded Config cannot say which fields a tag supports. Each tag's
 *          checked-in default configuration does say:
 *          `embedded/proto-c/<name>-proto-c/default-config.json` lists every
 *          field the tag supports, zeros included. The build embeds those
 *          files' text (see CMakeLists.txt), and ConfigShape reads them at run
 *          time into one set of paths per tag type, such as
 *          `"adxl362.freq"` or `"period"`.
 *
 *          A shape answers two questions the host needs:
 *          - has(): does this tag support this field? qtmonitor decides what
 *            to show from it.
 *          - merge(): copy from a configuration file only the fields the tag
 *            supports and the file actually sets. qtmonitor's Restore and
 *            `tag-start --merge` both use it.
 *
 * @see     docs/shared/design/proposals/config-shape-and-panels.md
 */

#ifndef TAGCORE_CONFIGSHAPE_H
#define TAGCORE_CONFIGSHAPE_H

#include <functional>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "tag.pb.h"

/**
 * @class   ConfigShape
 * @brief   A set of Config field paths, for one tag type or one JSON file.
 *
 * @details A path names a field by its .proto name, with nested messages
 *          joined by dots: `active_interval`, `active_interval.start_epoch`,
 *          `adxl362`, `adxl362.range`. A message field is listed as well as
 *          the fields inside it. A repeated message field (`hibernate`) is
 *          listed once, with the fields of its elements under it
 *          (`hibernate.start_epoch`). `tag_type` is never a path: it identifies
 *          the tag rather than configuring it.
 *
 *          Shapes are immutable once built. Instances returned by forTag() live
 *          until the program exits.
 */
class ConfigShape
{
public:
  /**
   * @brief   Shape of a tag type, from its embedded default config(s).
   *
   * @details Built for all tag types on the first call, then cached. Two
   *          default configs with the same `tag_type` (PresTag and PresTagRaw)
   *          are unioned. A default config that fails to parse is reported on
   *          stderr and in loadErrors(), and skipped.
   *
   * @param[in] tag_type  Tag type to look up.
   * @return  The shape, or an empty shape for a tag type with no default.
   *
   * @note    Thread-safe: the cache is a function-local static.
   */
  static const ConfigShape &forTag(TagType tag_type);

  /**
   * @brief   Shape of a JSON Config text: the keys it actually contains.
   *
   * @details Unlike parsing into Config, keys with zero values are kept, so a
   *          file that says `"period": 0` has `period` in its shape. Keys may
   *          use .proto names or JSON (lowerCamelCase) names.
   *
   * @param[in]  json   JSON text of a Config.
   * @param[out] error  Why the text was rejected; may be null.
   * @return  The shape, or empty when the text is not JSON or names a field
   *          Config does not have.
   */
  static std::optional<ConfigShape> fromJson(std::string_view json,
                                             std::string *error);

  /**
   * @brief   Shape of a configuration file: the fields it speaks for.
   *
   * @details qtmonitor's Save writes every Config field, padding the ones the
   *          tag does not have with zeros, so a file's keys over-state what it
   *          means. When the file names a `tag_type` that has a default
   *          config, its shape is fromJson() limited to forTag() of that type:
   *          a BitTagNG file speaks for BitTagNG's fields, and its padded
   *          `"bittag_log": "BITTAG_UNSPECIFIED"` is not copied onto a BitTag.
   *          A file with no known `tag_type` is taken at its word.
   *
   * @param[in]  json   JSON text of a Config.
   * @param[out] error  Why the text was rejected; may be null.
   * @return  The shape, or empty when fromJson() would reject the text.
   */
  static std::optional<ConfigShape> forFile(std::string_view json,
                                            std::string *error);

  /**
   * @brief   Reports whether a path names a field of Config at all.
   *
   * @param[in] path  Dotted path, e.g. `"adxl362.freq"`.
   * @return  true when every segment resolves through the Config descriptor.
   */
  static bool isConfigPath(std::string_view path);

  /**
   * @brief   Problems found reading the embedded default configs.
   *
   * @return  One message per default config that was skipped, naming the
   *          file and the reason; empty when every default loaded. Forces the
   *          shapes to be built.
   */
  static const std::vector<std::string> &loadErrors();

  /**
   * @brief   Reports whether this shape includes a field.
   *
   * @param[in] path  Dotted path of a Config field.
   * @return  true when the field is in the shape.
   *
   * @warning A path that names no Config field is a programming error, not an
   *          unsupported field: it aborts in debug builds, and is reported on
   *          stderr and answered false otherwise.
   */
  bool has(std::string_view path) const;

  /// Every path in the shape, sorted.
  const std::set<std::string, std::less<>> &paths() const { return paths_; }

  /**
   * @brief   Copies into @p target the fields that this shape includes and
   *          @p overlay_shape includes, taking their values from @p overlay.
   *
   * @details Scalars and enums are copied as they are, so a zero the overlay
   *          states is copied. Messages are merged field by field. The repeated
   *          `hibernate` is copied element by element, up to the smaller of
   *          the two counts. `tag_type` is never copied. Fields outside this
   *          shape are never added to @p target.
   *
   * @param[in,out] target         Configuration to update, normally the
   *                               tag's own.
   * @param[in]     overlay        Values to copy, normally a file parsed into
   *                               Config.
   * @param[in]     overlay_shape  Fields @p overlay speaks for: forFile() of
   *                               the same text.
   */
  void merge(Config &target, const Config &overlay,
             const ConfigShape &overlay_shape) const;

private:
  friend class ConfigShapeBuilder;
  std::set<std::string, std::less<>> paths_;
};

#endif // TAGCORE_CONFIGSHAPE_H
