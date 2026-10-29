/**
 * @file    preference_store.cpp
 * @brief   PreferenceStore: sensorViz's per-tag preference file, read and
 *          written without Qt.
 *
 * @details It replaced the `QJsonDocument` code in the Qt front end's
 *          `sensor_preferences.cpp` and keeps its file format byte for byte.
 *          Reading parses with protobuf's generic JSON form
 *          (google::protobuf::Value), as sensoranalysis's calibration reader
 *          does; writing is done here by hand, because no library writes Qt's
 *          indented layout. The color names are Qt's own table, taken from
 *          `QColor::colorNames()`.
 */
#include "preference_store.h"

#include <google/protobuf/struct.pb.h>
#include <google/protobuf/util/json_util.h>

#include <algorithm>
#include <cstdio>
#include <functional>
#include <set>
#include <utility>
#include <vector>

namespace sensorviz {
namespace {

/// The only preference file version.
constexpr int kPreferencesVersion = 1;

/// One SVG color name, as `QColor::colorNames()` lists it.
struct NamedColor
{
    const char *name; ///< Lower-case name.
    Rgb rgb;          ///< Its color.
};

/// Qt's color names and their colors, from `QColor::colorNames()` (Qt 6).
const NamedColor kNamedColors[] = {
    {"aliceblue", {240, 248, 255}}, {"antiquewhite", {250, 235, 215}}, {"aqua", {0, 255, 255}},
    {"aquamarine", {127, 255, 212}}, {"azure", {240, 255, 255}}, {"beige", {245, 245, 220}},
    {"bisque", {255, 228, 196}}, {"black", {0, 0, 0}}, {"blanchedalmond", {255, 235, 205}},
    {"blue", {0, 0, 255}}, {"blueviolet", {138, 43, 226}}, {"brown", {165, 42, 42}},
    {"burlywood", {222, 184, 135}}, {"cadetblue", {95, 158, 160}}, {"chartreuse", {127, 255, 0}},
    {"chocolate", {210, 105, 30}}, {"coral", {255, 127, 80}}, {"cornflowerblue", {100, 149, 237}},
    {"cornsilk", {255, 248, 220}}, {"crimson", {220, 20, 60}}, {"cyan", {0, 255, 255}},
    {"darkblue", {0, 0, 139}}, {"darkcyan", {0, 139, 139}}, {"darkgoldenrod", {184, 134, 11}},
    {"darkgray", {169, 169, 169}}, {"darkgreen", {0, 100, 0}}, {"darkgrey", {169, 169, 169}},
    {"darkkhaki", {189, 183, 107}}, {"darkmagenta", {139, 0, 139}}, {"darkolivegreen", {85, 107, 47}},
    {"darkorange", {255, 140, 0}}, {"darkorchid", {153, 50, 204}}, {"darkred", {139, 0, 0}},
    {"darksalmon", {233, 150, 122}}, {"darkseagreen", {143, 188, 143}}, {"darkslateblue", {72, 61, 139}},
    {"darkslategray", {47, 79, 79}}, {"darkslategrey", {47, 79, 79}}, {"darkturquoise", {0, 206, 209}},
    {"darkviolet", {148, 0, 211}}, {"deeppink", {255, 20, 147}}, {"deepskyblue", {0, 191, 255}},
    {"dimgray", {105, 105, 105}}, {"dimgrey", {105, 105, 105}}, {"dodgerblue", {30, 144, 255}},
    {"firebrick", {178, 34, 34}}, {"floralwhite", {255, 250, 240}}, {"forestgreen", {34, 139, 34}},
    {"fuchsia", {255, 0, 255}}, {"gainsboro", {220, 220, 220}}, {"ghostwhite", {248, 248, 255}},
    {"gold", {255, 215, 0}}, {"goldenrod", {218, 165, 32}}, {"gray", {128, 128, 128}},
    {"green", {0, 128, 0}}, {"greenyellow", {173, 255, 47}}, {"grey", {128, 128, 128}},
    {"honeydew", {240, 255, 240}}, {"hotpink", {255, 105, 180}}, {"indianred", {205, 92, 92}},
    {"indigo", {75, 0, 130}}, {"ivory", {255, 255, 240}}, {"khaki", {240, 230, 140}},
    {"lavender", {230, 230, 250}}, {"lavenderblush", {255, 240, 245}}, {"lawngreen", {124, 252, 0}},
    {"lemonchiffon", {255, 250, 205}}, {"lightblue", {173, 216, 230}}, {"lightcoral", {240, 128, 128}},
    {"lightcyan", {224, 255, 255}}, {"lightgoldenrodyellow", {250, 250, 210}}, {"lightgray", {211, 211, 211}},
    {"lightgreen", {144, 238, 144}}, {"lightgrey", {211, 211, 211}}, {"lightpink", {255, 182, 193}},
    {"lightsalmon", {255, 160, 122}}, {"lightseagreen", {32, 178, 170}}, {"lightskyblue", {135, 206, 250}},
    {"lightslategray", {119, 136, 153}}, {"lightslategrey", {119, 136, 153}}, {"lightsteelblue", {176, 196, 222}},
    {"lightyellow", {255, 255, 224}}, {"lime", {0, 255, 0}}, {"limegreen", {50, 205, 50}},
    {"linen", {250, 240, 230}}, {"magenta", {255, 0, 255}}, {"maroon", {128, 0, 0}},
    {"mediumaquamarine", {102, 205, 170}}, {"mediumblue", {0, 0, 205}}, {"mediumorchid", {186, 85, 211}},
    {"mediumpurple", {147, 112, 219}}, {"mediumseagreen", {60, 179, 113}}, {"mediumslateblue", {123, 104, 238}},
    {"mediumspringgreen", {0, 250, 154}}, {"mediumturquoise", {72, 209, 204}}, {"mediumvioletred", {199, 21, 133}},
    {"midnightblue", {25, 25, 112}}, {"mintcream", {245, 255, 250}}, {"mistyrose", {255, 228, 225}},
    {"moccasin", {255, 228, 181}}, {"navajowhite", {255, 222, 173}}, {"navy", {0, 0, 128}},
    {"oldlace", {253, 245, 230}}, {"olive", {128, 128, 0}}, {"olivedrab", {107, 142, 35}},
    {"orange", {255, 165, 0}}, {"orangered", {255, 69, 0}}, {"orchid", {218, 112, 214}},
    {"palegoldenrod", {238, 232, 170}}, {"palegreen", {152, 251, 152}}, {"paleturquoise", {175, 238, 238}},
    {"palevioletred", {219, 112, 147}}, {"papayawhip", {255, 239, 213}}, {"peachpuff", {255, 218, 185}},
    {"peru", {205, 133, 63}}, {"pink", {255, 192, 203}}, {"plum", {221, 160, 221}},
    {"powderblue", {176, 224, 230}}, {"purple", {128, 0, 128}}, {"red", {255, 0, 0}},
    {"rosybrown", {188, 143, 143}}, {"royalblue", {65, 105, 225}}, {"saddlebrown", {139, 69, 19}},
    {"salmon", {250, 128, 114}}, {"sandybrown", {244, 164, 96}}, {"seagreen", {46, 139, 87}},
    {"seashell", {255, 245, 238}}, {"sienna", {160, 82, 45}}, {"silver", {192, 192, 192}},
    {"skyblue", {135, 206, 235}}, {"slateblue", {106, 90, 205}}, {"slategray", {112, 128, 144}},
    {"slategrey", {112, 128, 144}}, {"snow", {255, 250, 250}}, {"springgreen", {0, 255, 127}},
    {"steelblue", {70, 130, 180}}, {"tan", {210, 180, 140}}, {"teal", {0, 128, 128}},
    {"thistle", {216, 191, 216}}, {"tomato", {255, 99, 71}}, {"transparent", {0, 0, 0}},
    {"turquoise", {64, 224, 208}}, {"violet", {238, 130, 238}}, {"wheat", {245, 222, 179}},
    {"white", {255, 255, 255}}, {"whitesmoke", {245, 245, 245}}, {"yellow", {255, 255, 0}},
    {"yellowgreen", {154, 205, 50}},
};

/// True for the ASCII whitespace characters.
bool isSpace(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r';
}

/// The value of one hex digit, or -1.
int hexDigit(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

/// Reads @p digits hex digits at @p text, or -1 if any is not one.
int hexValue(const char *text, int digits)
{
    int value = 0;
    for (int i = 0; i < digits; i++) {
        const int digit = hexDigit(text[i]);
        if (digit < 0) {
            return -1;
        }
        value = value * 16 + digit;
    }
    return value;
}

/// A 16-bit channel rounded to 8 bits as Qt rounds it (qt_div_257()).
int from16(int value)
{
    value += 0x80;
    return (value - (value >> 8)) >> 8;
}

/// @p text with spaces and tabs removed and ASCII letters lowered, as Qt
/// prepares a color name.
std::string lowerWithoutSpaces(const std::string &text)
{
    std::string out;
    for (char c : text) {
        if (c != ' ' && c != '\t') {
            out += (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
        }
    }
    return out;
}

/// `text.trimmed().toLower()` for ASCII.
std::string trimmedLower(const std::string &text)
{
    size_t begin = 0;
    size_t end = text.size();
    while (begin < end && isSpace(text[begin])) {
        begin++;
    }
    while (end > begin && isSpace(text[end - 1])) {
        end--;
    }
    std::string out = text.substr(begin, end - begin);
    for (char &c : out) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return out;
}

// --- Key order -------------------------------------------------------------

/**
 * @brief   The UTF-16 code units of UTF-8 @p text, for ordering only.
 *
 * @details Qt orders JSON object keys by UTF-16 code unit, which puts
 *          characters above U+FFFF (surrogate pairs, 0xD800-0xDFFF) before
 *          U+E000-U+FFFF, where UTF-8 byte order puts them after. Bytes that
 *          are not valid UTF-8 are taken one at a time.
 */
std::u16string utf16Key(const std::string &text)
{
    std::u16string units;
    for (size_t i = 0; i < text.size();) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        const int length = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xe ? 3 : (c >> 3) == 0x1e ? 4 : 1;
        if (length == 1 || i + static_cast<size_t>(length) > text.size()) {
            units.push_back(c);
            i++;
            continue;
        }
        char32_t point = c & (0x7f >> length);
        for (int k = 1; k < length; k++) {
            point = point << 6 | (static_cast<unsigned char>(text[i + static_cast<size_t>(k)]) & 0x3f);
        }
        if (point >= 0x10000) {
            point -= 0x10000;
            units.push_back(static_cast<char16_t>(0xd800 + (point >> 10)));
            units.push_back(static_cast<char16_t>(0xdc00 + (point & 0x3ff)));
        } else {
            units.push_back(static_cast<char16_t>(point));
        }
        i += static_cast<size_t>(length);
    }
    return units;
}

/// Orders keys as Qt's QJsonObject does.
struct Utf16Less
{
    /// True when @p a sorts before @p b in UTF-16 code units.
    bool operator()(const std::string &a, const std::string &b) const
    {
        return utf16Key(a) < utf16Key(b);
    }
};

/// The keys of @p map in Qt's order.
template <typename Map>
std::vector<typename Map::const_pointer> qtOrder(const Map &map)
{
    std::vector<typename Map::const_pointer> entries;
    for (const auto &entry : map) {
        entries.push_back(&entry);
    }
    std::stable_sort(entries.begin(), entries.end(), [](auto a, auto b) {
        return Utf16Less()(a->first, b->first);
    });
    return entries;
}

// --- Writing, in QJsonDocument::Indented's layout --------------------------

/// Appends @p text as a JSON string literal, escaped as Qt escapes it.
void writeString(std::string &out, const std::string &text)
{
    out += '"';
    for (unsigned char c : text) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20) {
                char escaped[8];
                std::snprintf(escaped, sizeof escaped, "\\u%04x", c);
                out += escaped;
            } else {
                out += static_cast<char>(c);
            }
        }
    }
    out += '"';
}

/// Appends four spaces per level.
void indent(std::string &out, int level)
{
    out.append(static_cast<size_t>(level) * 4, ' ');
}

/// Appends one member's value at an indent level.
using Writer = std::function<void(std::string &out, int level)>;

/// A writer for a string value.
Writer stringValue(std::string text)
{
    return [text = std::move(text)](std::string &out, int) { writeString(out, text); };
}

/// Appends an object of @p members, (key, value writer) pairs already in key order.
void writeObject(std::string &out, int level, const std::vector<std::pair<std::string, Writer>> &members)
{
    out += "{\n";
    for (size_t i = 0; i < members.size(); i++) {
        indent(out, level + 1);
        writeString(out, members[i].first);
        out += ": ";
        members[i].second(out, level + 1);
        out += i + 1 < members.size() ? ",\n" : "\n";
    }
    indent(out, level);
    out += '}';
}

/// Appends an array of strings in set (byte) order, as Qt appended them.
void writeStringArray(std::string &out, int level, const std::set<std::string> &items)
{
    out += "[\n";
    size_t i = 0;
    for (const std::string &item : items) {
        indent(out, level + 1);
        writeString(out, item);
        out += ++i < items.size() ? ",\n" : "\n";
    }
    indent(out, level);
    out += ']';
}

/// Appends one tag entry's overrides, keys in order.
void writeTag(std::string &out, int level, const TagPreferences &preferences)
{
    std::vector<std::pair<std::string, Writer>> members;
    if (!preferences.axisSides.empty()) {
        std::vector<std::pair<std::string, Writer>> sides;
        for (const auto *entry : qtOrder(preferences.axisSides)) {
            sides.emplace_back(entry->first, stringValue(entry->second == AxisSide::Right ? "right" : "left"));
        }
        members.emplace_back("axis_sides", [sides](std::string &o, int l) { writeObject(o, l, sides); });
    }
    if (preferences.hasBreakLinesAtGapsOverride) {
        const bool value = preferences.breakLinesAtGaps;
        members.emplace_back("break_lines_at_gaps",
                             [value](std::string &o, int) { o += value ? "true" : "false"; });
    }
    if (!preferences.streamColors.empty()) {
        std::vector<std::pair<std::string, Writer>> colors;
        for (const auto *entry : qtOrder(preferences.streamColors)) {
            colors.emplace_back(entry->first, stringValue(colorName(entry->second)));
        }
        members.emplace_back("colors", [colors](std::string &o, int l) { writeObject(o, l, colors); });
    }
    if (preferences.hasVisibleStreamOverride) {
        const std::set<std::string> &ids = preferences.visibleStreamIds;
        members.emplace_back("visible_streams",
                             [&ids](std::string &o, int l) { writeStringArray(o, l, ids); });
    }
    writeObject(out, level, members);
}

// --- Reading ---------------------------------------------------------------

using google::protobuf::Value;

/// A JSON object's members in key order, as Qt's QJsonObject iterates them.
std::map<std::string, const Value *, Utf16Less> sorted(const google::protobuf::Struct &object)
{
    std::map<std::string, const Value *, Utf16Less> members;
    for (const auto &field : object.fields()) {
        members.emplace(field.first, &field.second);
    }
    return members;
}

/// Reads one tag entry, as the Qt front end's jsonToPreferences() did.
bool readTag(const std::string &tag, const google::protobuf::Struct &object,
             TagPreferences &preferences, std::string &error)
{
    preferences = TagPreferences();
    preferences.tagType = tag;
    const auto &fields = object.fields();

    const auto visible = fields.find("visible_streams");
    if (visible != fields.end()) {
        if (visible->second.kind_case() != Value::kListValue) {
            error = "visible_streams for " + tag + " must be an array";
            return false;
        }
        preferences.hasVisibleStreamOverride = true;
        for (const Value &entry : visible->second.list_value().values()) {
            if (entry.kind_case() != Value::kStringValue) {
                error = "visible_streams for " + tag + " must contain only strings";
                return false;
            }
            preferences.visibleStreamIds.insert(entry.string_value());
        }
    }

    const auto colors = fields.find("colors");
    if (colors != fields.end()) {
        if (colors->second.kind_case() != Value::kStructValue) {
            error = "colors for " + tag + " must be an object";
            return false;
        }
        for (const auto &[id, value] : sorted(colors->second.struct_value())) {
            if (value->kind_case() != Value::kStringValue) {
                error = "colors." + id + " for " + tag + " must be a color string";
                return false;
            }
            Rgb color;
            if (!colorFromString(value->string_value(), color)) {
                error = "colors." + id + " for " + tag + " is not a valid color";
                return false;
            }
            preferences.streamColors[id] = color;
        }
    }

    const auto sides = fields.find("axis_sides");
    if (sides != fields.end()) {
        if (sides->second.kind_case() != Value::kStructValue) {
            error = "axis_sides for " + tag + " must be an object";
            return false;
        }
        for (const auto &[id, value] : sorted(sides->second.struct_value())) {
            const std::string side =
                value->kind_case() == Value::kStringValue ? trimmedLower(value->string_value()) : "";
            if (side != "left" && side != "right") {
                error = "axis_sides." + id + " for " + tag + " must be left or right";
                return false;
            }
            preferences.axisSides[id] = side == "right" ? AxisSide::Right : AxisSide::Left;
        }
    }

    const auto gaps = fields.find("break_lines_at_gaps");
    if (gaps != fields.end()) {
        if (gaps->second.kind_case() != Value::kBoolValue) {
            error = "break_lines_at_gaps for " + tag + " must be true or false";
            return false;
        }
        preferences.hasBreakLinesAtGapsOverride = true;
        preferences.breakLinesAtGaps = gaps->second.bool_value();
    }
    return true;
}

} // namespace

bool colorFromString(const std::string &text, Rgb &color)
{
    if (!text.empty() && text[0] == '#') {
        const char *hex = text.c_str() + 1;
        int r = -1;
        int g = -1;
        int b = -1;
        switch (text.size()) {
        case 4: // #rgb
            if (hexValue(hex, 3) < 0) {
                return false;
            }
            r = hexValue(hex, 1) * 0x11;
            g = hexValue(hex + 1, 1) * 0x11;
            b = hexValue(hex + 2, 1) * 0x11;
            break;
        case 7: // #rrggbb
            r = hexValue(hex, 2);
            g = hexValue(hex + 2, 2);
            b = hexValue(hex + 4, 2);
            break;
        case 9: // #aarrggbb
            if (hexValue(hex, 2) < 0) {
                return false;
            }
            r = hexValue(hex + 2, 2);
            g = hexValue(hex + 4, 2);
            b = hexValue(hex + 6, 2);
            break;
        case 10: // #rrrgggbbb
            r = hexValue(hex, 3);
            g = hexValue(hex + 3, 3);
            b = hexValue(hex + 6, 3);
            if (r < 0 || g < 0 || b < 0) {
                return false;
            }
            r = from16(r << 4 | r >> 8);
            g = from16(g << 4 | g >> 8);
            b = from16(b << 4 | b >> 8);
            break;
        case 13: // #rrrrggggbbbb
            r = hexValue(hex, 4);
            g = hexValue(hex + 4, 4);
            b = hexValue(hex + 8, 4);
            if (r < 0 || g < 0 || b < 0) {
                return false;
            }
            r = from16(r);
            g = from16(g);
            b = from16(b);
            break;
        default:
            return false;
        }
        if (r < 0 || g < 0 || b < 0) {
            return false;
        }
        color = Rgb{static_cast<std::uint8_t>(r), static_cast<std::uint8_t>(g),
                    static_cast<std::uint8_t>(b)};
        return true;
    }

    const std::string name = lowerWithoutSpaces(text);
    for (const NamedColor &named : kNamedColors) {
        if (name == named.name) {
            color = named.rgb;
            return true;
        }
    }
    return false;
}

std::string colorName(Rgb color)
{
    char name[8];
    std::snprintf(name, sizeof name, "#%02x%02x%02x", color.r, color.g, color.b);
    return name;
}

TagPreferences PreferenceStore::get(const std::string &tagType) const
{
    const auto it = byTag_.find(tagType);
    return it == byTag_.end() ? TagPreferences() : it->second;
}

void PreferenceStore::remember(const TagPreferences &preferences)
{
    if (preferences.hasOverrides()) {
        byTag_[preferences.tagType] = preferences;
    } else {
        byTag_.erase(preferences.tagType);
    }
}

void PreferenceStore::forget(const std::string &tagType)
{
    byTag_.erase(tagType);
}

bool PreferenceStore::load(const std::string &json, const std::string &source, std::string &error)
{
    Value document;
    const auto status = google::protobuf::util::JsonStringToMessage(json, &document);
    if (!status.ok()) {
        error = "Could not parse " + source + ": " + std::string(status.message());
        return false;
    }
    if (document.kind_case() != Value::kStructValue) {
        error = "Preference file " + source + " must contain a JSON object";
        return false;
    }
    const auto &root = document.struct_value().fields();

    // QJsonValue::toInt() is the value only for a number that is a whole int.
    const auto version = root.find("version");
    if (version != root.end()
        && !(version->second.kind_case() == Value::kNumberValue
             && version->second.number_value() == kPreferencesVersion)) {
        error = "Unsupported sensorViz preferences version in " + source;
        return false;
    }

    const auto tags = root.find("tags");
    if (tags == root.end() || tags->second.kind_case() != Value::kStructValue) {
        error = "Preference file " + source + " must contain a tags object";
        return false;
    }

    std::map<std::string, TagPreferences> loaded;
    for (const auto &[tag, value] : sorted(tags->second.struct_value())) {
        if (value->kind_case() != Value::kStructValue) {
            error = "Preferences for " + tag + " must be a JSON object";
            return false;
        }
        TagPreferences preferences;
        if (!readTag(tag, value->struct_value(), preferences, error)) {
            return false;
        }
        if (preferences.hasOverrides()) {
            loaded.emplace(tag, std::move(preferences));
        }
    }
    byTag_ = std::move(loaded);
    return true;
}

std::string PreferenceStore::save() const
{
    std::vector<std::pair<std::string, Writer>> tags;
    for (const auto *entry : qtOrder(byTag_)) {
        tags.emplace_back(entry->first, [entry](std::string &o, int l) { writeTag(o, l, entry->second); });
    }
    const std::vector<std::pair<std::string, Writer>> root = {
        {"tags", [&tags](std::string &o, int l) { writeObject(o, l, tags); }},
        {"version", [](std::string &o, int) { o += std::to_string(kPreferencesVersion); }},
    };
    std::string out;
    writeObject(out, 0, root);
    out += '\n';
    return out;
}

} // namespace sensorviz
