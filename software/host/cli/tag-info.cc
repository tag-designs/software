#include <stdint.h>
#include <string>
#include <vector>
#include <google/protobuf/message.h>
#include <google/protobuf/text_format.h>
#include <map>
#include <regex>
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
      std::cout << "Tag SHA " << str << std::endl;

    strMap["monitor"] = "Monitor Version";
    strMap["board_desc"] = "Board Description";
    strMap["uuid"] = "Chip UUID";
    strMap["intflashsz"] = "Internal Flash (kB)";
    strMap["extflashsz"] = "External Flash (kB)";
    strMap["firmware"] = "Firmware";
    strMap["gitrepo"] = "Git Repo";
    strMap["githash"] = "Git Hash";
    strMap["build_time"] = "Build Date";

    tag.GetTagInfo(info);

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
      std::string name = field->name();
      std::string type_name = field->type_name();

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
  }
  return 0;
}
