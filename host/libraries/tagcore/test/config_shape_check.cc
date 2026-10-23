/**
 * @file    config_shape_check.cc
 * @brief   Assertion checks for ConfigShape: loading, lookup and the merge.
 *
 * @details Checks that:
 *          - every embedded default config loads;
 *          - for every tag type, ConfigShape reproduces exactly the field
 *            visibility the retired build-time generator
 *            (cmake/GenerateConfigVisibility.cmake) produced. The table below
 *            is that generator's last output, frozen. It was the gate for
 *            deleting the generator, and it stays as a regression check;
 *          - has() and isConfigPath() tell an unsupported field from a
 *            non-field;
 *          - fromJson() keeps zero-valued keys, accepts JSON names and rejects
 *            unknown keys; forFile() limits a file to its own tag's fields;
 *          - merge() copies exactly the fields both the tag and the file have.
 *
 *          Build with -DBUILD_TAGCORE_CHECKS=ON and run with no arguments; see
 *          README.md. Prints "CONFIG SHAPE CHECK: all assertions passed".
 */

#undef NDEBUG
#include <cassert>
#include <cstdio>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include <google/protobuf/util/json_util.h>

#include "configshape.h"

namespace
{

/// ConfigFieldVisibility member -> the Config path it stood for.
const std::vector<std::pair<std::string, std::string>> kMemberPaths = {
    {"active_interval", "active_interval"},
    {"hibernate", "hibernate"},
    {"period", "period"},
    {"start_delay", "start_delay"},
    {"bittag_log", "bittag_log"},
    {"adxl362", "adxl362"},
    {"adxl362_range", "adxl362.range"},
    {"adxl362_freq", "adxl362.freq"},
    {"adxl362_filter", "adxl362.filter"},
    {"adxl362_act_thresh_g", "adxl362.act_thresh_g"},
    {"adxl362_inact_thresh_g", "adxl362.inact_thresh_g"},
    {"adxl362_inactive_sec", "adxl362.inactive_sec"},
    {"adxl362_accel_type", "adxl362.accel_type"},
    {"adxl362_data_format", "adxl362.data_format"},
    {"adxl362_channels", "adxl362.channels"},
    {"adxl362_active_sec", "adxl362.active_sec"},
    {"lsm6", "lsm6"},
    {"lsm6_odr", "lsm6.odr"},
    {"lsm6_accel_rng", "lsm6.accel_rng"},
    {"lsm6_gyro_rng", "lsm6.gyro_rng"},
};

/// The generator's last output: the members that were true, per tag type.
const std::map<TagType, std::vector<std::string>> kGeneratorTable = {
    {BITPRESTAG, {"active_interval", "hibernate", "adxl362", "adxl362_act_thresh_g",
                  "adxl362_inact_thresh_g", "adxl362_inactive_sec"}},
    {BITTAG, {"active_interval", "hibernate", "bittag_log", "adxl362", "adxl362_range",
              "adxl362_freq", "adxl362_filter", "adxl362_act_thresh_g",
              "adxl362_inact_thresh_g", "adxl362_inactive_sec"}},
    {BITTAGNG, {"active_interval", "hibernate", "adxl362", "adxl362_act_thresh_g",
                "adxl362_inactive_sec", "adxl362_accel_type"}},
    {BITTAG_LE, {"active_interval", "hibernate", "bittag_log", "adxl362",
                 "adxl362_act_thresh_g", "adxl362_inact_thresh_g", "adxl362_inactive_sec"}},
    {COMPASSTAG, {"active_interval", "hibernate"}},
    {IMUTAG, {"start_delay", "lsm6", "lsm6_odr", "lsm6_accel_rng", "lsm6_gyro_rng"}},
    {PRESTAG, {"active_interval", "hibernate", "period"}},
    {UIUCTAG, {"active_interval", "hibernate", "adxl362", "adxl362_act_thresh_g",
               "adxl362_inactive_sec"}},
};

Config parse(const std::string &json)
{
  Config c;
  const auto status = google::protobuf::util::JsonStringToMessage(json, &c);
  assert(status.ok());
  return c;
}

ConfigShape shapeOf(const std::string &json)
{
  std::string error;
  auto shape = ConfigShape::fromJson(json, &error);
  if (!shape)
    std::fprintf(stderr, "fromJson failed: %s\n", error.c_str());
  assert(shape);
  return *shape;
}

/// Merges @p file into @p tag the way Restore does, and returns the result.
Config restore(const Config &tag, const std::string &file)
{
  Config out = tag;
  std::string error;
  const auto file_shape = ConfigShape::forFile(file, &error);
  assert(file_shape);
  ConfigShape::forTag(tag.tag_type()).merge(out, parse(file), *file_shape);
  return out;
}

void checkLoading()
{
  for (const std::string &e : ConfigShape::loadErrors())
    std::fprintf(stderr, "load error: %s\n", e.c_str());
  assert(ConfigShape::loadErrors().empty());
}

void checkMatchesGenerator()
{
  for (const auto &[tag, trues] : kGeneratorTable)
  {
    const ConfigShape &shape = ConfigShape::forTag(tag);
    for (const auto &[member, path] : kMemberPaths)
    {
      bool expected = false;
      for (const std::string &t : trues)
        expected = expected || t == member;
      if (shape.has(path) != expected)
        std::fprintf(stderr, "%s: %s is %d, generator said %d\n",
                     std::string(TagType_Name(tag)).c_str(), path.c_str(),
                     shape.has(path), expected);
      assert(shape.has(path) == expected);
    }
  }
  assert(ConfigShape::forTag(TAG_UNSPECIFIED).paths().empty());
}

void checkLookup()
{
  const ConfigShape &ng = ConfigShape::forTag(BITTAGNG);
  assert(ng.has("adxl362.accel_type"));
  assert(!ng.has("adxl362.range"));         // supported by Config, not by BitTagNG
  assert(ConfigShape::isConfigPath("adxl362.range"));
  assert(!ConfigShape::isConfigPath("adxl362.frq"));
  assert(!ConfigShape::isConfigPath("tag_type"));
  assert(ConfigShape::isConfigPath("hibernate.start_epoch"));

  const ConfigShape &imu = ConfigShape::forTag(IMUTAG);
  assert(imu.has("lsm6.accel_rng"));        // the default config says "accelRng"
  assert(!imu.has("active_interval"));
  assert(ConfigShape::forTag(PRESTAG).has("period"));
  assert(ConfigShape::forTag(PRESTAG).has("hibernate.end_epoch"));
}

void checkFromJson()
{
  const ConfigShape zero = shapeOf(R"({"period": 0, "adxl362": {"range": "R2G"}})");
  assert(zero.has("period"));               // a zero value is still a key
  assert(zero.has("adxl362") && zero.has("adxl362.range"));
  assert(!zero.has("adxl362.freq"));

  assert(shapeOf(R"({"lsm6": {"accelRng": "R4G"}})").has("lsm6.accel_rng"));
  assert(shapeOf(R"({"hibernate": []})").has("hibernate"));

  std::string error;
  assert(!ConfigShape::fromJson(R"({"bogus": 1})", &error) && !error.empty());
  assert(!ConfigShape::fromJson(R"({"lsm6": {"range": 1}})", nullptr));  // wrong level
  assert(!ConfigShape::fromJson("{not json", nullptr));
}

void checkMerge()
{
  // A stated field is copied; an omitted one keeps the tag's value.
  Config ng = parse(R"({"tag_type": "BITTAGNG",
                        "adxl362": {"accel_type": "AdxlType_367", "act_thresh_g": 0.5,
                                    "inactive_sec": 6}})");
  Config out = restore(ng, R"({"adxl362": {"act_thresh_g": 0.3}})");
  assert(out.adxl362().act_thresh_g() == 0.3f);
  assert(out.adxl362().inactive_sec() == 6.0f);
  assert(out.adxl362().accel_type() == Adxl362_AdxlType_AdxlType_367);

  // A stated zero is copied.
  Config pres = parse(R"({"tag_type": "PRESTAG", "period": 60})");
  assert(restore(pres, R"({"period": 0})").period() == 0);

  // A field the tag does not support is not added.
  Config le = parse(R"({"tag_type": "BITTAG_LE", "adxl362": {"act_thresh_g": 0.5}})");
  assert(restore(le, R"({"adxl362": {"freq": "S400"}})").adxl362().freq() == 0);

  // Another tag's full file speaks only for that tag's fields: the zeros Save
  // pads it with are not copied, and nothing the target lacks is added.
  Config imu = parse(R"({"tag_type": "IMUTAG", "start_delay": 1,
                         "lsm6": {"odr": "S100", "accel_rng": "R4G", "gyro_rng": "R1000dps"}})");
  const std::string prestag_file =
      R"({"tag_type": "PRESTAG", "period": 60, "start_delay": 0, "bittag_log": "BITTAG_UNSPECIFIED",
          "active_interval": {"start_epoch": 0, "end_epoch": 2147483647},
          "hibernate": [{"start_epoch": 0, "end_epoch": 0}]})";
  out = restore(imu, prestag_file);
  assert(out.tag_type() == IMUTAG);         // tag_type is never copied
  assert(out.start_delay() == 1);           // PresTag has no start_delay: padding
  assert(!out.has_active_interval() && out.period() == 0 && out.hibernate_size() == 0);
  assert(out.lsm6().odr() == Lsm6dsv_ODR_S100);

  // A file with no tag_type is taken at its word.
  assert(restore(imu, R"({"start_delay": 0})").start_delay() == 0);

  // A BitTagNG file's padded bittag_log does not reach a BitTag; a BitTag LE
  // file's real one does.
  Config bittag = parse(R"({"tag_type": "BITTAG", "bittag_log": "BITTAG_BITSPERMIN"})");
  assert(restore(bittag, R"({"tag_type": "BITTAGNG", "bittag_log": "BITTAG_UNSPECIFIED"})")
             .bittag_log() == BITTAG_BITSPERMIN);
  assert(restore(bittag, R"({"tag_type": "BITTAG_LE", "bittag_log": "BITTAG_BITPERSEC"})")
             .bittag_log() == BITTAG_BITPERSEC);

  // Part of a message: only the stated field changes.
  Config bt = parse(R"({"tag_type": "BITTAG", "bittag_log": "BITTAG_BITSPERMIN",
                        "active_interval": {"start_epoch": 100, "end_epoch": 200},
                        "hibernate": [{"start_epoch": 1, "end_epoch": 2},
                                      {"start_epoch": 3, "end_epoch": 4}]})");
  out = restore(bt, R"({"active_interval": {"start_epoch": 150}})");
  assert(out.active_interval().start_epoch() == 150);
  assert(out.active_interval().end_epoch() == 200);

  // hibernate: element by element, up to the tag's own count.
  out = restore(bt, R"({"hibernate": [{"start_epoch": 10, "end_epoch": 20},
                                      {"start_epoch": 30, "end_epoch": 40},
                                      {"start_epoch": 50, "end_epoch": 60}]})");
  assert(out.hibernate_size() == 2);
  assert(out.hibernate(0).start_epoch() == 10 && out.hibernate(1).end_epoch() == 40);
  out = restore(bt, R"({"hibernate": [{"start_epoch": 10, "end_epoch": 20}]})");
  assert(out.hibernate_size() == 2);
  assert(out.hibernate(0).end_epoch() == 20 && out.hibernate(1).start_epoch() == 3);

  // An empty file changes nothing.
  assert(restore(bt, "{}").SerializeAsString() == bt.SerializeAsString());
}

} // namespace

/**
 * @brief Runs every check; any failure aborts on its assertion.
 * @return 0 when every assertion passed.
 */
int main()
{
  checkLoading();
  checkMatchesGenerator();
  checkLookup();
  checkFromJson();
  checkMerge();
  std::printf("CONFIG SHAPE CHECK: all assertions passed\n");
  return 0;
}
