#include "cli.h"
using namespace google::protobuf;

typedef std::function<bool(Tag &, std::stringstream &)> command;
typedef std::map<std::string, command> command_dict;

static std::map<std::string, std::string> strMap = {
    {"monitor", "Monitor Version"},
    {"board_desc", "Board Description"},
    {"uuid", "Chip UUID"},
    {"intflashsz", "Internal Flash (kB)"},
    {"extflashsz", "External Flash (kB)"},
    {"firmware", "Firmware"},
    {"gitrepo", "Git Repo"},
    {"githash", "Git Hash"},
    {"build_time", "Build Date"}};

static bool info(Tag &tag, std::stringstream &args)
{
    TagInfo info;
    if (!tag.GetTagInfo(info))
    {
        return false;
    }
    else
    {
        const auto reflection = info.GetReflection();

        const Descriptor *desc = info.GetDescriptor();
        int fieldCount = desc->field_count();

        for (int i = 0; i < fieldCount; i++)
        {
            const FieldDescriptor *field = desc->field(i);
            std::string value;
            std::string name = field->name();

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

            if (strMap.count(name))
            {
                std::cout << strMap[name] << " : " << value << std::endl;
            }
            else
            {
                std::cout << name << " : " << value << std::endl;
            }
        }
        return true;
    }
}

static bool status(Tag &tag, std::stringstream &args)
{
    Status s;
    if (!tag.GetStatus(s))
    {
        return false;
    }
    else
    {
        std::string formatted;
        TextFormat::PrintToString(s, &formatted);
        std::cout << formatted << std::endl;
        return true;
    }
}

static bool stop(Tag &tag, std::stringstream &args)
{
    if (!tag.Stop())
    {
        return false;
    }
    else
    {
        return true;
    }
}

static bool voltage(Tag &tag, std::stringstream &args)
{
    float f;
    if (!tag.Voltage(f))
    {
        return false;
    }
    else
    {
        std::cout << "Voltage: " << f << std::endl;
        return true;
    }
}

static bool state(Tag &tag, std::stringstream &args)
{
    Status s;
    std::string formatted;
    if (!tag.GetStatus(s))
        return false;
    std::cout << "state: " << TagState_Name(s.state()) << std::endl;
    return true;
}

static bool erase(Tag &tag, std::stringstream &args)
{
    std::string formatted;
    if (!tag.Erase())
        return false;
    return true;
}

static bool echo(Tag &tag, std::stringstream &args)
{
    std::string token;

    while (args >> token)
    {
        std::cout << " " << token;
    }
    std::cout << std::endl;
    return true;
}

command_dict c = {
    {"status", &status},
    {"echo", &echo},
    {"erase",&erase},
    {"info", &info},
    {"stop", &stop},
    {"state", &state},
    {"voltage", &voltage}};

bool cmd_eval(Tag &tag, std::string &name, std::stringstream &args)
{
    auto it = c.find(name);
    if (it != end(c))
    {
        return (it->second)(tag, args); 
    }
    else
    {
        std::cerr << "unknown command:  " << name << std::endl;
        return false;
    }
}