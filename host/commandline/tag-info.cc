#include <stdint.h>
#include <string>
#include <vector>
#include <google/protobuf/message.h>
#include <google/protobuf/text_format.h>
#include <map>
#include <regex>
#include <ctime>
#include <iostream>
#include <sstream>
using namespace google::protobuf;

#include <cxxopts.hpp>

extern "C"
{
#include "log.h"
}

#ifdef _WIN64
#include <csignal>
#else
#include <signal.h>
#endif

#include <tag.pb.h>
#include <tagclass.h>

extern bool parse_options(int argc, char **argv,
                          cxxopts::Options &options,
                          Tag &tag,
                          UsbDev &dev);

static void intHandler(int dummy)
{
  (void)dummy;
  exit(1);
}

static std::map<std::string, std::string> strMap;

/**
 * @brief Render a marker epoch as UTC, or as a bare value when implausible.
 *
 * @param[in] millis Marker timestamp in milliseconds since the Unix epoch.
 * @return Human-readable timestamp, suffixed with a warning when the value is
 *         not a plausible acquisition time.
 */
static std::string formatMarkerTime(int64_t millis)
{
  const int64_t seconds = millis / 1000;
  char buf[64];

  // The tag stores -1 as the erased-slot sentinel, and an unsynchronized RTC
  // yields an epoch near zero, so call both out rather than printing a date.
  if (seconds <= 0)
    return std::to_string(seconds) + "  <-- clock not set";

  const std::time_t t = static_cast<std::time_t>(seconds);
  std::tm tm_utc{};
#ifdef _WIN64
  gmtime_s(&tm_utc, &t);
#else
  gmtime_r(&t, &tm_utc);
#endif
  if (std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S UTC", &tm_utc) == 0)
    return std::to_string(seconds);
  return std::string(buf);
}

/**
 * @brief Print the tag's persistent state-transition marker log.
 *
 * @details The tag records one marker per state transition into a fixed-size,
 *          non-wrapping region of internal flash that only a reset erases. Once
 *          it fills, further markers are dropped silently, which freezes the
 *          history that boot-time reset recovery reads to reconstruct a lost
 *          state. The entry count is therefore the diagnostic of interest: a
 *          count at the tag's capacity means recovery may be acting on a stale
 *          final marker.
 *
 * @param[in,out] tag Attached tag to query.
 */
/** @brief IMUTag collection-init failure bits; see IMUTag inc/sensors.h. */
enum {
  kInitFailLsmConfig = 1u << 0,
  kInitFailMag       = 1u << 1,
  kInitFailPressure  = 1u << 2,
  kInitFailStatusShift = 8,
  kInitFailStatusMask  = 0xffu << kInitFailStatusShift,
};

/**
 * @brief Render a marker's detail word for the reason that produced it.
 *
 * @details The word is reason-scoped by design, so decoding is per-event. Only
 *          the abort detail is decoded today; anything else is shown in hex
 *          rather than silently dropped, so a newer firmware reporting detail
 *          this tool does not know about is still visible.
 *
 * @param[in] reason Transition reason the detail belongs to.
 * @param[in] detail Raw detail word from the marker.
 * @return Human-readable description, never empty.
 */
static std::string formatTransitionDetail(State_Event reason, uint32_t detail)
{
  std::ostringstream out;

  if (reason != State_Event_EVENT_UNKNOWN) {
    out << "0x" << std::hex << detail;
    return out.str();
  }

  // Collection init failed; name the stages so the failing device is known
  // without a debug build. See initDataCollection() in the IMUTag family.
  const char *sep = "";
  out << "collection init failed: ";
  if (detail & kInitFailLsmConfig) { out << sep << "LSM6 config"; sep = ", "; }
  if (detail & kInitFailMag)       { out << sep << "magnetometer"; sep = ", "; }
  if (detail & kInitFailPressure)  { out << sep << "pressure"; sep = ", "; }
  if (*sep == '\0')
    out << "unrecognized (0x" << std::hex << detail << std::dec << ")";

  const uint32_t status = (detail & kInitFailStatusMask) >> kInitFailStatusShift;
  if (status != 0)
    out << "; first driver status " << status;
  return out.str();
}

static void printStateLog(Tag &tag)
{
  StateLog system_log;
  int next = 0;
  int page_count = 0;

  std::cout << "State transition log:" << std::endl;

  // Bound the paging loop: a tag that keeps returning entries must not hang the
  // tool. Capacity is 10 + TagState_MAX on current firmware, so this is ample.
  while (tag.GetStateLog(system_log, next) && (page_count++ < 16))
  {
    const int returned = system_log.states().size();
    if (returned <= 0)
      break;

    for (const auto &state : system_log.states())
    {
      std::cout << "  [" << next << "] "
                << TagState_Name(state.status().state())
                << "  reason=" << State_Event_Name(state.transition_reason())
                << "  time=" << formatMarkerTime(state.status().millis())
                << std::endl;
      // Reason-scoped diagnostic the firmware stored with the marker. Only
      // present on targets whose flash record has room for it, and only for
      // transitions that had something to say, so absence means nothing.
      if (state.transition_detail() != 0)
        std::cout << "        detail=" << formatTransitionDetail(
                         state.transition_reason(), state.transition_detail())
                  << std::endl;
      std::cout << "        internal_pages=" << state.status().internal_data_count()
                << " external_pages=" << state.status().external_data_count()
                << " vdd=" << state.status().voltage()
                << " temp=" << state.status().temperature()
                << std::endl;
      next++;
    }
  }

  if (next == 0)
  {
    std::cout << "  (empty -- nothing recorded since the last reset; reset"
                 " recovery reads this as idle)" << std::endl;
  }
  else
  {
    std::cout << "  entries: " << next
              << "  (compare against the tag's capacity, 10 + TagState_MAX;"
                 " at capacity, new markers are dropped silently and reset"
                 " recovery may act on a stale final entry)" << std::endl;
  }
}

int main(int argc, char **argv)
{
  Tag tag;
  UsbDev dev;

  cxxopts::Options options("tag-info",
                           "print tag information");

  signal(SIGINT, intHandler);

  if (parse_options(argc, argv, options, tag, dev) && tag.Attach(dev))
  {
    std::string str;
    TagInfo info;
    if (tag.GitSha(str))
      std::cout << "Tag SHA: " << str << std::endl;

    strMap["monitor"] = "Monitor Version";
    strMap["board_desc"] = "Board Description";
    strMap["uuid"] = "Chip UUID";
    strMap["intflashsz"] = "Internal Flash (kB)";
    strMap["extflashsz"] = "External Flash (kB)";
    strMap["firmware"] = "Firmware";
    strMap["gitrepo"] = "Git Repo";
    strMap["githash"] = "Git Hash";
    strMap["build_time"] = "Build Date";

    if (!tag.GetTagInfo(info))
    {
      std::cerr << "Info failed\n";

    } else {

      const auto reflection = info.GetReflection();
      std::string formatted;
      TextFormat::PrintToString(info, &formatted);
      std::cout << formatted << std::endl;

      const Descriptor *desc = info.GetDescriptor();
      int fieldCount = desc->field_count();
      //fprintf(stderr, "The fullname of the message is %s \n", desc->full_name().c_str());
      for (int i = 0; i < fieldCount; i++)
      {
        const FieldDescriptor *field = desc->field(i);
        std::string value;
        std::string name(field->name());
        std::string type_name(field->type_name());

        switch (field->type())
        {
        case FieldDescriptor::TYPE_INT64:
          value = std::to_string(reflection->GetInt64(info, field));
          break;
        case FieldDescriptor::TYPE_STRING:
          value = reflection->GetString(info, field);
          break;
        case FieldDescriptor::TYPE_ENUM:
          value = reflection->GetEnum(info, field)->name();
          break;
        default:
          value = "Unexpected Field Type";
        }

        std::cout << type_name << ": ";

        if (strMap.count(name))
        {
          std::cout << strMap[name] << " : " << value << std::endl;
        }
        else
        {
          std::cout << name << " : " << value << std::endl;
        }
      }
      Config cfg;
      tag.GetConfig(cfg);
      std::cout << cfg.DebugString() << std::endl;

      Status status;
      if (tag.GetStatus(status))
      {
        std::cout << "Current state: " << TagState_Name(status.state())
                  << std::endl;
        // Firmware fills this with a boot reset-recovery trace when it has
        // nothing else to report; see statusRecoveryTraceWrite(). It explains
        // which state the tag booted into and why, which the live state and
        // the marker log below cannot when the two disagree.
        if (!status.debug_message().empty())
          std::cout << "Firmware report: " << status.debug_message()
                    << std::endl;
      }
      printStateLog(tag);
    }
  }
  return 0;
}
