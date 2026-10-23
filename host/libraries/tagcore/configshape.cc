/**
 * @file    configshape.cc
 * @brief   ConfigShape: building shapes from JSON, lookup, and the merge.
 */

#include "configshape.h"

#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <map>

#include <google/protobuf/descriptor.h>
#include <google/protobuf/struct.pb.h>
#include <google/protobuf/util/json_util.h>

using google::protobuf::Descriptor;
using google::protobuf::FieldDescriptor;
using google::protobuf::Message;
using google::protobuf::Reflection;

/// One embedded default config: its directory name and its JSON text.
struct EmbeddedDefaultConfig
{
  const char *name;  ///< e.g. "bittag-ng-proto-c"
  const char *text;  ///< contents of that directory's default-config.json
};

/// Every tag's default-config.json, in directory-name order. Defined in the
/// generated defaultconfigs.cc (see this directory's CMakeLists.txt).
extern const EmbeddedDefaultConfig kEmbeddedDefaultConfigs[];
/// Number of entries in kEmbeddedDefaultConfigs (one placeholder entry, not
/// counted, when there are none).
extern const std::size_t kEmbeddedDefaultConfigCount;

namespace
{

/// Path segments of a dotted path.
std::vector<std::string_view> split(std::string_view path)
{
  std::vector<std::string_view> segs;
  for (std::size_t start = 0;;)
  {
    const std::size_t dot = path.find('.', start);
    segs.push_back(path.substr(start, dot == std::string_view::npos ? dot : dot - start));
    if (dot == std::string_view::npos)
      return segs;
    start = dot + 1;
  }
}

/// Field of @p type named @p key, by .proto name or JSON name.
const FieldDescriptor *findField(const Descriptor &type, const std::string &key)
{
  if (const FieldDescriptor *f = type.FindFieldByName(key))
    return f;
  for (int i = 0; i < type.field_count(); ++i)
    if (type.field(i)->json_name() == key)
      return type.field(i);
  return nullptr;
}

/// Every dotted path of every field reachable from @p type, except tag_type.
void allPaths(const Descriptor &type, const std::string &prefix,
              std::set<std::string, std::less<>> &out)
{
  for (int i = 0; i < type.field_count(); ++i)
  {
    const FieldDescriptor *f = type.field(i);
    if (prefix.empty() && f->name() == "tag_type")
      continue;
    const std::string path = prefix + std::string(f->name());
    out.insert(path);
    if (f->cpp_type() == FieldDescriptor::CPPTYPE_MESSAGE)
      allPaths(*f->message_type(), path + ".", out);
  }
}

const std::set<std::string, std::less<>> &validPaths()
{
  static const std::set<std::string, std::less<>> paths = [] {
    std::set<std::string, std::less<>> out;
    allPaths(*Config::descriptor(), "", out);
    return out;
  }();
  return paths;
}

/**
 * Records the path of every key under @p object, checked against @p type.
 * Descends into objects and into the first element of arrays of objects.
 * @return false, with @p error set, at the first key that is not a field.
 */
bool walk(const google::protobuf::Struct &object, const Descriptor &type,
          const std::string &prefix, std::set<std::string, std::less<>> &paths,
          std::string *error)
{
  for (const auto &[key, value] : object.fields())
  {
    if (prefix.empty() && key == "tag_type")
      continue;
    const FieldDescriptor *field = findField(type, key);
    if (field == nullptr)
    {
      if (error)
        *error = "no Config field " + prefix + key;
      return false;
    }
    const std::string path = prefix + std::string(field->name());
    paths.insert(path);
    if (field->cpp_type() != FieldDescriptor::CPPTYPE_MESSAGE)
      continue;
    const google::protobuf::Struct *child = nullptr;
    if (value.has_struct_value())
      child = &value.struct_value();
    else if (value.has_list_value() && value.list_value().values_size() > 0 &&
             value.list_value().values(0).has_struct_value())
      child = &value.list_value().values(0).struct_value();
    if (child && !walk(*child, *field->message_type(), path + ".", paths, error))
      return false;
  }
  return true;
}

/// Parses JSON text into a Struct, then walks it. @return false on any error.
bool shapeOf(std::string_view json, std::set<std::string, std::less<>> &paths,
             std::string *tag_type, std::string *error)
{
  google::protobuf::Struct object;
  const auto status = google::protobuf::util::JsonStringToMessage(
      std::string(json), &object);
  if (!status.ok())
  {
    if (error)
      *error = "not a JSON object: " + std::string(status.message());
    return false;
  }
  if (tag_type)
  {
    const auto it = object.fields().find("tag_type");
    *tag_type = it == object.fields().end() ? "" : it->second.string_value();
  }
  return walk(object, *Config::descriptor(), "", paths, error);
}

/// Copies one scalar or enum field's value from @p from to @p to.
void copyScalar(const Message &from, Message &to, const FieldDescriptor *f)
{
  const Reflection *rf = from.GetReflection();
  const Reflection *rt = to.GetReflection();
  switch (f->cpp_type())
  {
  case FieldDescriptor::CPPTYPE_INT32: rt->SetInt32(&to, f, rf->GetInt32(from, f)); break;
  case FieldDescriptor::CPPTYPE_INT64: rt->SetInt64(&to, f, rf->GetInt64(from, f)); break;
  case FieldDescriptor::CPPTYPE_UINT32: rt->SetUInt32(&to, f, rf->GetUInt32(from, f)); break;
  case FieldDescriptor::CPPTYPE_UINT64: rt->SetUInt64(&to, f, rf->GetUInt64(from, f)); break;
  case FieldDescriptor::CPPTYPE_FLOAT: rt->SetFloat(&to, f, rf->GetFloat(from, f)); break;
  case FieldDescriptor::CPPTYPE_DOUBLE: rt->SetDouble(&to, f, rf->GetDouble(from, f)); break;
  case FieldDescriptor::CPPTYPE_BOOL: rt->SetBool(&to, f, rf->GetBool(from, f)); break;
  case FieldDescriptor::CPPTYPE_ENUM: rt->SetEnumValue(&to, f, rf->GetEnumValue(from, f)); break;
  case FieldDescriptor::CPPTYPE_STRING: rt->SetString(&to, f, rf->GetString(from, f)); break;
  case FieldDescriptor::CPPTYPE_MESSAGE: break;
  }
}

} // namespace

/**
 * @brief Builds the per-tag shapes from the embedded default configs; a friend
 *        of ConfigShape so it can fill them.
 */
class ConfigShapeBuilder
{
public:
  /// Everything read from the embedded default configs.
  struct Table
  {
    std::map<TagType, ConfigShape> shapes;  ///< One shape per tag type with a default.
    std::vector<std::string> errors;        ///< One message per default that was skipped.
  };

  /**
   * @brief The table, built on first use and kept for the program's life.
   * @note  Thread-safe: a function-local static.
   */
  static const Table &table()
  {
    static const Table built = build();
    return built;
  }

  /// A shape holding exactly @p paths, for fromJson() and forFile().
  static ConfigShape make(std::set<std::string, std::less<>> paths)
  {
    ConfigShape shape;
    shape.paths_ = std::move(paths);
    return shape;
  }

private:
  static Table build()
  {
    Table t;
    for (std::size_t i = 0; i < kEmbeddedDefaultConfigCount; ++i)
    {
      const EmbeddedDefaultConfig &d = kEmbeddedDefaultConfigs[i];
      std::set<std::string, std::less<>> paths;
      std::string tag_name;
      std::string error;
      TagType tag_type = TAG_UNSPECIFIED;
      if (!shapeOf(d.text, paths, &tag_name, &error))
      {
        // error already says why
      }
      else if (!TagType_Parse(tag_name, &tag_type) || tag_type == TAG_UNSPECIFIED)
      {
        error = "no valid tag_type";
      }
      else
      {
        t.shapes[tag_type].paths_.insert(paths.begin(), paths.end());
        continue;
      }
      t.errors.push_back(std::string(d.name) + "/default-config.json: " + error);
      std::fprintf(stderr, "ConfigShape: skipping %s\n", t.errors.back().c_str());
    }
    return t;
  }
};

const ConfigShape &ConfigShape::forTag(TagType tag_type)
{
  static const ConfigShape empty;
  const auto &shapes = ConfigShapeBuilder::table().shapes;
  const auto it = shapes.find(tag_type);
  return it == shapes.end() ? empty : it->second;
}

std::optional<ConfigShape> ConfigShape::fromJson(std::string_view json,
                                                 std::string *error)
{
  std::set<std::string, std::less<>> paths;
  if (!shapeOf(json, paths, nullptr, error))
    return std::nullopt;
  return ConfigShapeBuilder::make(std::move(paths));
}

std::optional<ConfigShape> ConfigShape::forFile(std::string_view json,
                                                std::string *error)
{
  std::set<std::string, std::less<>> paths;
  std::string tag_name;
  if (!shapeOf(json, paths, &tag_name, error))
    return std::nullopt;
  TagType tag_type = TAG_UNSPECIFIED;
  if (TagType_Parse(tag_name, &tag_type) && tag_type != TAG_UNSPECIFIED)
  {
    const auto &own = forTag(tag_type).paths_;
    if (!own.empty())
    {
      for (auto it = paths.begin(); it != paths.end();)
        it = own.count(*it) ? std::next(it) : paths.erase(it);
    }
  }
  return ConfigShapeBuilder::make(std::move(paths));
}

bool ConfigShape::isConfigPath(std::string_view path)
{
  return validPaths().count(path) != 0;
}

const std::vector<std::string> &ConfigShape::loadErrors()
{
  return ConfigShapeBuilder::table().errors;
}

bool ConfigShape::has(std::string_view path) const
{
  if (!isConfigPath(path))
  {
    std::fprintf(stderr, "ConfigShape::has(\"%.*s\"): not a Config field\n",
                 static_cast<int>(path.size()), path.data());
#ifndef NDEBUG
    std::abort();
#endif
    return false;
  }
  return paths_.count(path) != 0;
}

void ConfigShape::merge(Config &target, const Config &overlay,
                        const ConfigShape &overlay_shape) const
{
  std::string skip_under;   // descendants of a repeated field already copied
  for (const std::string &path : paths_)
  {
    if (!skip_under.empty() && path.rfind(skip_under, 0) == 0)
      continue;
    skip_under.clear();
    if (overlay_shape.paths_.count(path) == 0)
      continue;

    // Walk both messages to the field's parent, creating parents in target.
    Message *to = &target;
    const Message *from = &overlay;
    const FieldDescriptor *field = nullptr;
    const std::vector<std::string_view> segs = split(path);
    bool reached = true;
    for (std::size_t i = 0; i < segs.size(); ++i)
    {
      field = to->GetDescriptor()->FindFieldByName(std::string(segs[i]));
      if (field == nullptr)
      {
        reached = false;
        break;
      }
      if (i + 1 == segs.size())
        break;
      if (field->is_repeated())
      {
        // Only reached for paths under a repeated field whose own path is
        // missing from one shape; nothing sensible to copy.
        reached = false;
        break;
      }
      to = to->GetReflection()->MutableMessage(to, field);
      from = &from->GetReflection()->GetMessage(*from, field);
    }
    if (!reached || field == nullptr)
      continue;

    if (field->is_repeated())
    {
      if (field->cpp_type() == FieldDescriptor::CPPTYPE_MESSAGE)
      {
        const Reflection *rt = to->GetReflection();
        const Reflection *rf = from->GetReflection();
        const int n = std::min(rt->FieldSize(*to, field), rf->FieldSize(*from, field));
        for (int i = 0; i < n; ++i)
          rt->MutableRepeatedMessage(to, field, i)
              ->CopyFrom(rf->GetRepeatedMessage(*from, field, i));
      }
      skip_under = path + ".";
      continue;
    }
    if (field->cpp_type() == FieldDescriptor::CPPTYPE_MESSAGE)
      continue;   // its fields are separate paths
    copyScalar(*from, *to, field);
  }
}
