/**
 * @file    config_probe_cases.cc
 * @brief   Generates the case files `qtmonitor --config-probe` runs.
 *
 * @details For every tag type with a default config under
 *          `embedded/proto-c/<name>-proto-c/default-config.json`, writes one case
 *          file, `<tag_type>.json`. Each case is a Config to display, the tag
 *          state it is reported in, a fixed "now", and optionally the text of
 *          a file to restore over it. The cases are:
 *
 *          - the base configuration, in IDLE and in RUNNING. It is taken from
 *            the captured fixture in `host/docs/fixtures/qtmonitor/` when there
 *            is one, since that is real firmware output, and from the default
 *            config otherwise;
 *          - each field in the tag's default config, set in turn to each enum
 *            value (plus 0 and an undefined value) or to a fixed list of
 *            numbers covering zero, negatives, off-step and out-of-range;
 *          - schedule start and end times around "now", and hibernation rows;
 *          - the ADXL362 fields that constrain each other: range against
 *            thresholds, sample rate against inactivity time;
 *          - restores: the tag's own full file, one field at a time, explicit
 *            zeros, every other tag's file, and malformed files.
 *
 *          Fields are found by walking the default config's keys against the
 *          Config descriptor, so a field added to a default config is covered
 *          without changing this program. Output is deterministic: rerunning
 *          on an unchanged tree reproduces the committed files byte for byte.
 *
 *          Usage: config_probe_cases <source-root> <out-dir>
 *
 * @see     host/applications/qtmon/test/README.md
 */

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

#include <google/protobuf/descriptor.h>
#include <google/protobuf/struct.pb.h>
#include <google/protobuf/util/json_util.h>

#include "tag.pb.h"

namespace fs = std::filesystem;
using google::protobuf::Descriptor;
using google::protobuf::FieldDescriptor;
using google::protobuf::Message;
using google::protobuf::Reflection;

namespace
{

/// The fixed "now" every case runs at: 2026-10-08 12:02:03 UTC, deliberately
/// not on a minute boundary so the schedule's rounding is exercised.
constexpr int64_t kNow = 1791460923;
constexpr int64_t kForever = INT32_MAX;

/// Values tried for each float field.
const std::vector<double> kFloats = {0, 0.01, 0.123, 0.2, 0.5, 1, 1.5, 3, 6, 12, 300, -1};
/// Values tried for each integer field.
const std::vector<int64_t> kInts = {0, 1, 2, 59, 60, 61, 255, 256, 1440, 100000};
/// An enum number no Config enum defines.
constexpr int kUndefinedEnum = 99;

[[noreturn]] void fail(const std::string &why)
{
  std::fprintf(stderr, "config_probe_cases: %s\n", why.c_str());
  std::exit(1);
}

std::string readFile(const fs::path &path)
{
  std::ifstream in(path, std::ios::binary);
  if (!in)
    fail("cannot read " + path.string());
  std::ostringstream text;
  text << in.rdbuf();
  return text.str();
}

/// JSON string literal for @p s.
std::string quote(const std::string &s)
{
  std::string out = "\"";
  for (const char c : s)
  {
    switch (c)
    {
    case '"': out += "\\\""; break;
    case '\\': out += "\\\\"; break;
    case '\n': out += "\\n"; break;
    case '\t': out += "\\t"; break;
    case '\r': out += "\\r"; break;
    default:
      if (static_cast<unsigned char>(c) < 0x20)
      {
        char buf[8];
        std::snprintf(buf, sizeof buf, "\\u%04x", c);
        out += buf;
      }
      else
      {
        out += c;
      }
    }
  }
  return out + "\"";
}

/// Shortest decimal text that reads back as the same float.
std::string floatText(double v)
{
  char buf[32];
  std::snprintf(buf, sizeof buf, "%.9g", v);
  return buf;
}

/// Config as JSON, as a tag would send it: zero-valued fields omitted.
std::string wireJson(const Config &config)
{
  google::protobuf::util::JsonPrintOptions options;
  options.preserve_proto_field_names = true;
  std::string text;
  if (!google::protobuf::util::MessageToJsonString(config, &text, options).ok())
    fail("cannot print a Config");
  return text;
}

/// Config as JSON, as qtmonitor's Save writes it: every field printed.
template <typename Options>
auto setAlwaysPrint(Options &o, int) -> decltype(o.always_print_fields_with_no_presence = true, void())
{
  o.always_print_fields_with_no_presence = true;
}
template <typename Options>
auto setAlwaysPrint(Options &o, long) -> decltype(o.always_print_primitive_fields = true, void())
{
  o.always_print_primitive_fields = true;
}
std::string savedJson(const Config &config)
{
  google::protobuf::util::JsonPrintOptions options;
  options.preserve_proto_field_names = true;
  options.add_whitespace = true;
  setAlwaysPrint(options, 0);
  std::string text;
  if (!google::protobuf::util::MessageToJsonString(config, &text, options).ok())
    fail("cannot print a Config");
  return text;
}

Config parseConfig(const std::string &json, const std::string &where)
{
  Config config;
  if (!google::protobuf::util::JsonStringToMessage(json, &config).ok())
    fail(where + " does not parse as a Config");
  return config;
}

/// Field at one level, by .proto name or JSON name.
const FieldDescriptor *findField(const Descriptor &type, const std::string &key)
{
  if (const FieldDescriptor *f = type.FindFieldByName(key))
    return f;
  for (int i = 0; i < type.field_count(); ++i)
    if (type.field(i)->json_name() == key)
      return type.field(i);
  return nullptr;
}

/// Records every key under @p object as a dotted .proto path.
void walk(const google::protobuf::Struct &object, const Descriptor &type,
          const std::string &prefix, const std::string &file, std::set<std::string> &paths)
{
  for (const auto &[key, value] : object.fields())
  {
    if (prefix.empty() && key == "tag_type")
      continue;
    const FieldDescriptor *field = findField(type, key);
    if (field == nullptr)
      fail(file + ": no Config field " + prefix + key);
    const std::string path = prefix + std::string(field->name());
    paths.insert(path);
    if (field->type() != FieldDescriptor::TYPE_MESSAGE)
      continue;
    if (value.has_struct_value())
      walk(value.struct_value(), *field->message_type(), path + ".", file, paths);
    else if (value.has_list_value() && value.list_value().values_size() > 0 &&
             value.list_value().values(0).has_struct_value())
      walk(value.list_value().values(0).struct_value(), *field->message_type(),
           path + ".", file, paths);
  }
}

/// Resolves a dotted path to its field, creating parent messages in @p msg.
std::pair<Message *, const FieldDescriptor *> resolve(Message &msg, const std::string &path)
{
  Message *m = &msg;
  size_t start = 0;
  while (true)
  {
    const size_t dot = path.find('.', start);
    const std::string seg = path.substr(start, dot - start);
    const FieldDescriptor *f = m->GetDescriptor()->FindFieldByName(seg);
    if (f == nullptr)
      fail("bad path " + path);
    if (dot == std::string::npos)
      return {m, f};
    m = m->GetReflection()->MutableMessage(m, f);
    start = dot + 1;
  }
}

/// Field type of a dotted path, without modifying anything.
const FieldDescriptor *fieldAt(const std::string &path)
{
  Config scratch;
  return resolve(scratch, path).second;
}

void setNumber(Message &msg, const std::string &path, double value)
{
  auto [m, f] = resolve(msg, path);
  const Reflection *r = m->GetReflection();
  switch (f->cpp_type())
  {
  case FieldDescriptor::CPPTYPE_FLOAT: r->SetFloat(m, f, static_cast<float>(value)); break;
  case FieldDescriptor::CPPTYPE_DOUBLE: r->SetDouble(m, f, value); break;
  case FieldDescriptor::CPPTYPE_INT32: r->SetInt32(m, f, static_cast<int32_t>(value)); break;
  case FieldDescriptor::CPPTYPE_UINT32: r->SetUInt32(m, f, static_cast<uint32_t>(value)); break;
  case FieldDescriptor::CPPTYPE_INT64: r->SetInt64(m, f, static_cast<int64_t>(value)); break;
  case FieldDescriptor::CPPTYPE_UINT64: r->SetUInt64(m, f, static_cast<uint64_t>(value)); break;
  case FieldDescriptor::CPPTYPE_ENUM: r->SetEnumValue(m, f, static_cast<int>(value)); break;
  default: fail("cannot set " + path + " to a number");
  }
}

bool isScalar(const FieldDescriptor *f)
{
  return !f->is_repeated() && f->cpp_type() != FieldDescriptor::CPPTYPE_MESSAGE;
}

/// One case, already rendered as a JSON object.
struct Case
{
  std::string id;
  std::string state = "IDLE";
  Config config;
  bool has_restore = false;
  std::string restore;
};

std::string render(const Case &c)
{
  std::string out = "{\"id\": " + quote(c.id) + ", \"now\": " + std::to_string(kNow) +
                    ", \"state\": " + quote(c.state) + ", \"config\": " + wireJson(c.config);
  if (c.has_restore)
    out += ", \"restore\": " + quote(c.restore);
  return out + "}";
}

/// JSON member `"a": {"b": <leaf>}` for the dotted path `a.b`.
std::string nestedMember(const std::string &path, const std::string &leaf)
{
  std::vector<std::string> segs;
  for (size_t start = 0;;)
  {
    const size_t dot = path.find('.', start);
    segs.push_back(path.substr(start, dot - start));
    if (dot == std::string::npos)
      break;
    start = dot + 1;
  }
  std::string value = leaf;
  for (size_t i = segs.size(); i-- > 1;)
    value = "{" + quote(segs[i]) + ": " + value + "}";
  return quote(segs[0]) + ": " + value;
}

/// Text value for a case id: enum names where defined.
std::string valueName(const FieldDescriptor *f, double v)
{
  if (f->cpp_type() == FieldDescriptor::CPPTYPE_ENUM)
  {
    if (const auto *ev = f->enum_type()->FindValueByNumber(static_cast<int>(v)))
      return std::string(ev->name());
    return std::to_string(static_cast<int>(v));
  }
  if (f->cpp_type() == FieldDescriptor::CPPTYPE_FLOAT || f->cpp_type() == FieldDescriptor::CPPTYPE_DOUBLE)
    return floatText(v);
  return std::to_string(static_cast<int64_t>(v));
}

/// Values to try for one scalar field.
std::vector<double> valuesFor(const FieldDescriptor *f)
{
  std::vector<double> values;
  switch (f->cpp_type())
  {
  case FieldDescriptor::CPPTYPE_ENUM:
    for (int i = 0; i < f->enum_type()->value_count(); ++i)
      values.push_back(f->enum_type()->value(i)->number());
    values.push_back(kUndefinedEnum);
    break;
  case FieldDescriptor::CPPTYPE_FLOAT:
  case FieldDescriptor::CPPTYPE_DOUBLE:
    values.assign(kFloats.begin(), kFloats.end());
    break;
  default:
    for (const int64_t v : kInts)
      values.push_back(static_cast<double>(v));
  }
  return values;
}

/// All cases for one tag type.
std::vector<Case> casesFor(const std::string &tag, const Config &base,
                           const std::set<std::string> &paths,
                           const std::map<std::string, Config> &all_bases)
{
  std::vector<Case> cases;
  auto add = [&](const std::string &id, const Config &config) {
    Case c;
    c.id = id;
    c.config = config;
    cases.push_back(c);
    return &cases.back();
  };

  add("base", base);
  add("base@RUNNING", base)->state = "RUNNING";

  // One field at a time.
  for (const std::string &path : paths)
  {
    if (path.rfind("active_interval", 0) == 0 || path.rfind("hibernate", 0) == 0)
      continue;
    const FieldDescriptor *f = fieldAt(path);
    if (!isScalar(f))
      continue;
    for (const double v : valuesFor(f))
    {
      Config c = base;
      setNumber(c, path, v);
      add(path + "=" + valueName(f, v), c);
    }
  }

  // Schedule.
  if (paths.count("active_interval"))
  {
    const std::vector<std::pair<std::string, int64_t>> starts = {
        {"0", 0}, {"now-1h", kNow - 3600}, {"now+1m", kNow + 60},
        {"now+1d+1234s", kNow + 86400 + 1234}, {"forever", kForever}};
    const std::vector<std::pair<std::string, int64_t>> ends = {
        {"forever", kForever}, {"now+7d", kNow + 7 * 86400}, {"now-1m", kNow - 60}, {"0", 0}};
    for (const auto &[sname, s] : starts)
      for (const auto &[ename, e] : ends)
      {
        Config c = base;
        c.mutable_active_interval()->set_start_epoch(static_cast<int32_t>(s));
        c.mutable_active_interval()->set_end_epoch(static_cast<int32_t>(e));
        add("active_interval=" + sname + ".." + ename, c);
      }
  }
  if (paths.count("hibernate"))
  {
    const std::vector<std::tuple<std::string, int64_t, int64_t>> rows = {
        {"unset", 0, 0}, {"forever", kForever, kForever},
        {"now+1h..now+2h", kNow + 3600, kNow + 7200},
        {"now+2h..now+1h", kNow + 7200, kNow + 3600},
        {"now-2h..now-1h", kNow - 7200, kNow - 3600}};
    for (const auto &[name, s, e] : rows)
    {
      Config c = base;
      c.clear_hibernate();
      auto *h = c.add_hibernate();
      h->set_start_epoch(static_cast<int32_t>(s));
      h->set_end_epoch(static_cast<int32_t>(e));
      c.add_hibernate();
      add("hibernate[0]=" + name, c);
    }
    for (const int count : {0, 1, 3})
    {
      Config c = base;
      c.clear_hibernate();
      for (int i = 0; i < count; ++i)
      {
        auto *h = c.add_hibernate();
        h->set_start_epoch(static_cast<int32_t>(kNow + 3600 * (i + 1)));
        h->set_end_epoch(static_cast<int32_t>(kNow + 3600 * (i + 1) + 1800));
      }
      add("hibernate.count=" + std::to_string(count), c);
    }
  }

  // ADXL362 fields that constrain each other.
  if (paths.count("adxl362.range"))
  {
    for (const auto *rng : {"R2G", "R4G", "R8G"})
      for (const char *thresh : {"adxl362.act_thresh_g", "adxl362.inact_thresh_g"})
      {
        if (!paths.count(thresh))
          continue;
        for (const double t : {0.01, 0.5, 1.5, 3.0, 6.0})
        {
          Config c = base;
          c.mutable_adxl362()->set_range(
              static_cast<Adxl362_Rng>(Adxl362_Rng_descriptor()->FindValueByName(rng)->number()));
          setNumber(c, thresh, t);
          add(std::string("adxl362.range=") + rng + "+" + thresh + "=" + floatText(t), c);
        }
      }
  }
  if (paths.count("adxl362.freq") && paths.count("adxl362.inactive_sec"))
  {
    const auto *odr = Adxl362_Odr_descriptor();
    for (int i = 0; i < odr->value_count(); ++i)
    {
      if (odr->value(i)->number() == 0)
        continue;
      for (const double t : {0.01, 0.03, 0.1, 1.0, 5.0, 30.0, 300.0})
      {
        Config c = base;
        c.mutable_adxl362()->set_freq(static_cast<Adxl362_Odr>(odr->value(i)->number()));
        c.mutable_adxl362()->set_inactive_sec(static_cast<float>(t));
        add("adxl362.freq=" + std::string(odr->value(i)->name()) + "+inactive_sec=" + floatText(t), c);
      }
    }
  }

  // Restores, all in IDLE.
  auto restore = [&](const std::string &id, const std::string &text) {
    Case *c = add("restore:" + id, base);
    c->has_restore = true;
    c->restore = text;
  };
  restore("own-full", savedJson(base));
  for (const std::string &path : paths)
  {
    if (path.rfind("hibernate", 0) == 0)
      continue;
    const FieldDescriptor *f = fieldAt(path);
    if (!isScalar(f))
      continue;
    Config one;
    one.set_tag_type(base.tag_type());
    double v = 7;
    if (f->cpp_type() == FieldDescriptor::CPPTYPE_ENUM)
      v = f->enum_type()->value(f->enum_type()->value_count() - 1)->number();
    else if (f->cpp_type() == FieldDescriptor::CPPTYPE_FLOAT ||
             f->cpp_type() == FieldDescriptor::CPPTYPE_DOUBLE)
      v = 0.75;
    else if (path.rfind("active_interval", 0) == 0)
      v = static_cast<double>(kNow + 2 * 86400);
    setNumber(one, path, v);
    restore("only:" + path, wireJson(one));

    // The same field written as an explicit zero, which proto3 JSON accepts
    // although the wire cannot carry it.
    std::string zero = "0";
    if (f->cpp_type() == FieldDescriptor::CPPTYPE_ENUM)
      zero = quote(std::string(f->enum_type()->value(0)->name()));
    restore("zero:" + path,
            "{\"tag_type\": " + quote(std::string(TagType_Name(base.tag_type()))) + ", " +
                nestedMember(path, zero) + "}");
  }
  for (const auto &[other, other_base] : all_bases)
    if (other != tag)
      restore("foreign:" + other, savedJson(other_base));
  restore("empty-object", "{}");
  restore("malformed", "{not json");
  restore("unknown-key", "{\"bogus\": 1}");
  restore("wrong-type", "{\"period\": \"abc\"}");
  return cases;
}

} // namespace

/**
 * @brief Writes one case file per tag type.
 *
 * @param[in] argc  Must be 3.
 * @param[in] argv  argv[1]: repository root; argv[2]: output directory,
 *                  created if missing.
 * @return 0 on success; 2 on a usage error; 1, with a message on stderr, if a
 *         default config or fixture cannot be read or parsed.
 */
int main(int argc, char **argv)
{
  if (argc != 3)
  {
    std::fprintf(stderr, "usage: config_probe_cases <source-root> <out-dir>\n");
    return 2;
  }
  const fs::path root = argv[1];
  const fs::path out = argv[2];

  // Shapes and default bases from the default configs, in a fixed order.
  std::vector<fs::path> defaults;
  for (const auto &dir : fs::directory_iterator(root / "embedded/proto-c"))
  {
    const fs::path f = dir.path() / "default-config.json";
    if (dir.is_directory() && dir.path().filename().string().size() > 8 &&
        dir.path().filename().string().rfind("-proto-c") ==
            dir.path().filename().string().size() - 8 &&
        fs::exists(f))
      defaults.push_back(f);
  }
  std::sort(defaults.begin(), defaults.end());

  std::map<std::string, std::set<std::string>> shapes;
  std::map<std::string, Config> bases;
  std::map<std::string, std::string> base_source;
  for (const fs::path &file : defaults)
  {
    const std::string text = readFile(file);
    google::protobuf::Struct json;
    if (!google::protobuf::util::JsonStringToMessage(text, &json).ok())
      fail(file.string() + " is not JSON");
    const Config config = parseConfig(text, file.string());
    const std::string tag(TagType_Name(config.tag_type()));
    walk(json, *Config::descriptor(), "", file.string(), shapes[tag]);
    if (!bases.count(tag))
    {
      bases[tag] = config;
      base_source[tag] = fs::relative(file, root).string();
    }
  }

  // Captured fixtures replace the defaults as bases: they are real firmware output.
  std::vector<fs::path> fixtures;
  for (const auto &f : fs::directory_iterator(root / "host/docs/fixtures/qtmonitor"))
    if (f.path().extension() == ".json")
      fixtures.push_back(f.path());
  std::sort(fixtures.begin(), fixtures.end());
  for (const fs::path &file : fixtures)
  {
    google::protobuf::Struct json;
    if (!google::protobuf::util::JsonStringToMessage(readFile(file), &json).ok())
      fail(file.string() + " is not JSON");
    const auto config_it = json.fields().find("config");
    if (config_it == json.fields().end())
      continue;
    const auto &config_fields = config_it->second.struct_value().fields();
    const auto value_it = config_fields.find("value");
    if (value_it == config_fields.end())
      continue;
    std::string value_text;
    if (!google::protobuf::util::MessageToJsonString(value_it->second, &value_text).ok())
      fail(file.string() + ": cannot reprint config.value");
    const Config config = parseConfig(value_text, file.string());
    const std::string tag(TagType_Name(config.tag_type()));
    if (!shapes.count(tag))
      fail(file.string() + ": fixture for " + tag + ", which has no default config");
    bases[tag] = config;
    base_source[tag] = fs::relative(file, root).string();
  }

  fs::create_directories(out);
  for (const auto &[tag, paths] : shapes)
  {
    const std::vector<Case> cases = casesFor(tag, bases[tag], paths, bases);
    std::string name = tag;
    for (char &ch : name)
      ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    std::ofstream file(out / (name + ".json"), std::ios::binary | std::ios::trunc);
    file << "{\"schema\": \"qtmon-config-probe-cases/1\",\n";
    file << " \"tag_type\": " << quote(tag) << ",\n";
    file << " \"base_source\": " << quote(base_source[tag]) << ",\n";
    file << " \"cases\": [\n";
    for (size_t i = 0; i < cases.size(); ++i)
      file << "  " << render(cases[i]) << (i + 1 < cases.size() ? ",\n" : "\n");
    file << " ]}\n";
    std::printf("%s: %zu cases from %s\n", tag.c_str(), cases.size(), base_source[tag].c_str());
  }
  return 0;
}
