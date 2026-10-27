/**
 * @file    configdocument.h
 * @brief   qtmonitor's configuration document: one Config, where it came
 *          from, whether it has unsaved changes, and whether the user has
 *          approved it for Start.
 *
 * @details The Configuration tab (ConfigTab) shows the document as panels and
 *          makes the dialogs; everything about the document itself is here,
 *          with no Qt: reading a configuration file onto its tag type's
 *          defaults, the text a save writes, the unsaved-changes baseline,
 *          and approval. See host/applications/qtmon/design/architecture.md
 *          and editor-and-start-checks.md.
 */

#ifndef QTMON_CORE_CONFIGDOCUMENT_H
#define QTMON_CORE_CONFIGDOCUMENT_H

#include <optional>
#include <string>

#include "tag.pb.h"

namespace qtmon {

/// Where the document came from.
enum class DocumentSource {
  None,      ///< No document.
  Defaults,  ///< A tag type's default configuration (New).
  File,      ///< A configuration file (Open, or after Save).
  Tag,       ///< The attached tag's configuration (attach, Read).
};

/// The result of reading a configuration file (ConfigDocument::parseFile()).
struct ParsedFile {
  /// Outcome of reading the file.
  enum class Result {
    Ok,          ///< @c config holds the document to load.
    ParseError,  ///< The text is not a valid Config; @c detail says why.
    NoDefaults,  ///< Its tag type has no default config; @c detail says which.
  };
  Result result = Result::ParseError;  ///< See Result.
  Config config;       ///< The tag type's defaults with the file merged on top; for Ok.
  std::string detail;  ///< Why it failed; empty for Ok.
};

/**
 * @class   ConfigDocument
 * @brief   The configuration being edited: its values, source, file,
 *          unsaved-changes baseline and approval.
 *
 * @details "Modified" compares what the panels would write now with what they
 *          wrote just after loading (the baseline), because only the panels
 *          know how their controls round and clamp; the caller passes both
 *          in. Approval is of the values to be sent, so every load, reset,
 *          clear or edit withdraws it (contentChanged()); saving does not.
 *
 *          Not thread-safe; owned by the Configuration tab on the GUI thread.
 */
class ConfigDocument {
public:
  /// True when a document is loaded.
  bool hasDocument() const { return source_ != DocumentSource::None; }
  /// Where the document came from.
  DocumentSource source() const { return source_; }
  /// The document's tag type; TAG_UNSPECIFIED with no document.
  TagType tagType() const { return hasDocument() ? tag_type_ : TAG_UNSPECIFIED; }
  /// The document's file, for source File; empty otherwise.
  const std::string &path() const { return path_; }
  /// The values as last loaded, sent or saved; the panels' edits are not here.
  const Config &config() const { return config_; }

  /**
   * @brief Makes @p config the document.
   *
   * @details Withdraws approval. The caller then loads the panels and sets
   *          the baseline with setBaseline(), except for a reset, which keeps
   *          the old baseline so the reset shows as unsaved changes.
   *
   * @param[in] config  The values; its tag_type is the document's tag type.
   * @param[in] source  Where they came from.
   * @param[in] path    The file, for DocumentSource::File; otherwise empty.
   */
  void load(const Config &config, DocumentSource source, const std::string &path);

  /**
   * @brief Records what the panels wrote just after loading, against which
   *        isModified() compares.
   * @param[in] written  What the panels wrote, or nullptr when they could not
   *                     write the loaded values (every later state then
   *                     counts as modified).
   */
  void setBaseline(const Config *written);

  /**
   * @brief Whether the panels' values differ from the baseline.
   * @param[in] written  What the panels would write now, or nullptr when a
   *                     panel holds a value it cannot write.
   * @return false with no document; true when @p written is null or differs.
   */
  bool isModified(const Config *written) const;

  /// Records values that were sent or saved as the document's values.
  void setConfig(const Config &config) { config_ = config; }

  /**
   * @brief The document was saved to @p path with @p written as its values:
   *        it becomes a file document with no unsaved changes. Approval is
   *        unchanged, since the values are.
   */
  void saved(const std::string &path, const Config &written);

  /// Unloads the document and withdraws approval.
  void clear();

  /// Whether the user has approved the values for Start.
  bool approved() const { return approved_; }
  /// Records the user's approval; ignored with no document.
  void setApproved(bool approved) { approved_ = approved && hasDocument(); }
  /// The values to be sent may have changed: withdraws approval.
  void contentChanged() { approved_ = false; }

  /**
   * @brief Reads a configuration file as a document's values, without
   *        loading it.
   *
   * @details The result is the tag type's defaults with the file merged on
   *          top (ConfigShape::forFile() and ConfigShape::merge()): a full
   *          file saved by qtmonitor loads as written, and a partial file
   *          still gives a complete configuration. The tag type is the
   *          file's `tag_type`, or @p fallback_type when it names none.
   *
   * @param[in] json_text      Contents of a configuration file.
   * @param[in] fallback_type  Tag type for a file that names none.
   */
  static ParsedFile parseFile(const std::string &json_text, TagType fallback_type);

  /**
   * @brief Whether @p json_text is a valid Config that names no tag type,
   *        so the user must be asked which it is for.
   */
  static bool namesNoTagType(const std::string &json_text);

  /**
   * @brief The text Save writes: protobuf JSON with every field, including
   *        those at their default, under its proto field name.
   * @return The text, or nullopt when protobuf cannot print @p config.
   */
  static std::optional<std::string> fileText(const Config &config);

private:
  Config config_;                            ///< See config().
  TagType tag_type_ = TAG_UNSPECIFIED;       ///< See tagType().
  DocumentSource source_ = DocumentSource::None;  ///< See source().
  std::string path_;                         ///< See path().
  std::string baseline_;                     ///< What the panels wrote after loading, serialised.
  bool approved_ = false;                    ///< See approved().
};

} // namespace qtmon

#endif // QTMON_CORE_CONFIGDOCUMENT_H
