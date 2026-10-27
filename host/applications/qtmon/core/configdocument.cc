/**
 * @file    configdocument.cc
 * @brief   ConfigDocument, declared in configdocument.h.
 */

#include "configdocument.h"

#include <google/protobuf/util/json_util.h>

#include "configshape.h"

namespace qtmon {

namespace {

/**
 * @brief Sets the protobuf JSON option that prints zero-valued fields, under
 *        whichever name this protobuf version uses.
 *
 * @details One of an SFINAE pair: newer protobuf releases name the option
 *          always_print_fields_with_no_presence, older ones
 *          always_print_primitive_fields. Each overload's return type only
 *          compiles when its member exists. Call with a literal @c 0: the
 *          @c int overload (new name) is preferred when both are viable.
 *
 * @param[in,out] options  Print options to modify.
 * @param[in]     value    true to print fields that hold their default value.
 */
template <typename Options>
auto setAlwaysPrintDefaultFields(Options &options, bool value, int)
    -> decltype(options.always_print_fields_with_no_presence = value, void())
{
  options.always_print_fields_with_no_presence = value;
}

/// Fallback of the pair above, for protobuf's older option name.
template <typename Options>
auto setAlwaysPrintDefaultFields(Options &options, bool value, long)
    -> decltype(options.always_print_primitive_fields = value, void())
{
  options.always_print_primitive_fields = value;
}

} // namespace

void ConfigDocument::load(const Config &config, DocumentSource source,
                          const std::string &path)
{
  config_ = config;
  tag_type_ = config.tag_type();
  source_ = source;
  path_ = path;
  approved_ = false;
}

void ConfigDocument::setBaseline(const Config *written)
{
  baseline_ = written ? written->SerializeAsString() : std::string();
}

bool ConfigDocument::isModified(const Config *written) const
{
  if (!hasDocument())
    return false;
  return !written || written->SerializeAsString() != baseline_;
}

void ConfigDocument::saved(const std::string &path, const Config &written)
{
  config_ = written;
  source_ = DocumentSource::File;
  path_ = path;
  baseline_ = written.SerializeAsString();
}

void ConfigDocument::clear()
{
  config_.Clear();
  tag_type_ = TAG_UNSPECIFIED;
  source_ = DocumentSource::None;
  path_.clear();
  baseline_.clear();
  approved_ = false;
}

ParsedFile ConfigDocument::parseFile(const std::string &json_text, TagType fallback_type)
{
  ParsedFile out;
  Config parsed;
  const auto status = google::protobuf::util::JsonStringToMessage(json_text, &parsed);
  if (!status.ok()) {
    out.detail = status.ToString();
    return out;
  }
  std::string shape_error;
  const std::optional<ConfigShape> file_shape = ConfigShape::forFile(json_text, &shape_error);
  if (!file_shape) {
    out.detail = shape_error;
    return out;
  }

  const TagType type = parsed.tag_type() != TAG_UNSPECIFIED ? parsed.tag_type() : fallback_type;
  std::optional<Config> document = ConfigShape::defaultConfig(type);
  if (!document) {
    out.result = ParsedFile::Result::NoDefaults;
    out.detail = "No default configuration for tag type " + std::string(TagType_Name(type));
    return out;
  }
  ConfigShape::forTag(type).merge(*document, parsed, *file_shape);
  out.result = ParsedFile::Result::Ok;
  out.config = *document;
  return out;
}

bool ConfigDocument::namesNoTagType(const std::string &json_text)
{
  Config probe;
  return google::protobuf::util::JsonStringToMessage(json_text, &probe).ok() &&
         probe.tag_type() == TAG_UNSPECIFIED;
}

std::optional<std::string> ConfigDocument::fileText(const Config &config)
{
  google::protobuf::util::JsonPrintOptions options;
  options.add_whitespace = true;
  setAlwaysPrintDefaultFields(options, true, 0);
  options.preserve_proto_field_names = true;
  std::string text;
  if (!google::protobuf::util::MessageToJsonString(config, &text, options).ok())
    return std::nullopt;
  return text;
}

} // namespace qtmon
