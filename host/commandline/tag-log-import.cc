/**
 * @file    tag-log-import.cc
 * @brief   Turn a tagclient JSONL download into a SQLite log, without the tag.
 *
 * @details Reads the download format written by
 *          host/python/tagclient/examples/tag_dwnld.py and writes it with the
 *          same SqliteTagLogWriter that tag-dwnld uses:
 *
 *              tag-log-import download.jsonl -o tag.db3
 *
 *          The first line is a header object. Its `config` and `info` members
 *          are required; `states` (a list of State) and `calibration` (a list
 *          of CalibrationConstants, oldest first) are optional, and any other
 *          member (`tag_sha`, `status`) is ignored. Every later line is one
 *          Ack in protobuf JSON, written with one writeLog() call each, in
 *          order. Blank lines are skipped.
 *
 *          The tool is tag-agnostic: any tag type the writer supports is
 *          accepted. It is how tagsim (host/python/tagsim) turns simulated
 *          pages into a genuine log. The info table records
 *          source = import and the input path, plus any --info KEY=VALUE pairs.
 *
 *          Exit status: 0 written, 1 not. On failure the partial output file
 *          is removed, so it cannot pass for an import.
 *
 * @see     host/libraries/tagcore/sqlitelog.h, host/commandline/README.md
 */

#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include <google/protobuf/struct.pb.h>
#include <google/protobuf/util/json_util.h>
#include <cxxopts.hpp>

#include "sqlitelog.h"
#include "taglogwriter.h"

namespace {

namespace pbu = google::protobuf::util;

/**
 * @brief   Parse one member of the header object into a typed message.
 *
 * @details The header is parsed generically as a Struct, so each member is
 *          printed back to JSON and parsed again as its own message type.
 *          Unknown fields are an error, as a misspelt field would otherwise
 *          be silently dropped.
 *
 * @param[in]  value  The member's JSON value.
 * @param[out] out    The message to fill.
 * @param[in]  what   Member name, for the error message.
 * @param[out] error  Why it failed.
 * @return  false when the member is not valid JSON for the message type.
 */
bool ParseMember(const google::protobuf::Value &value, google::protobuf::Message &out,
                 const std::string &what, std::string &error) {
  std::string json;
  if (!pbu::MessageToJsonString(value, &json).ok()) {
    error = "cannot re-serialize header member " + what;
    return false;
  }
  const auto status = pbu::JsonStringToMessage(json, &out);
  if (!status.ok()) {
    error = "header member " + what + ": " + std::string(status.message());
    return false;
  }
  return true;
}

/**
 * @brief   Parse a header member that is a list of messages.
 *
 * @tparam  T  Message type of each element.
 * @param[in]  root   The header object.
 * @param[in]  key    Member name; an absent member gives an empty list.
 * @param[out] out    Parsed elements, in file order, appended.
 * @param[out] error  Why it failed.
 * @return  false when the member is not a list or an element does not parse.
 */
template <typename T>
bool ParseList(const google::protobuf::Struct &root, const std::string &key,
               std::vector<T> &out, std::string &error) {
  const auto it = root.fields().find(key);
  if (it == root.fields().end())
    return true;
  if (!it->second.has_list_value()) {
    error = "header member " + key + " is not a list";
    return false;
  }
  for (const auto &element : it->second.list_value().values()) {
    T message;
    if (!ParseMember(element, message, key, error))
      return false;
    out.push_back(message);
  }
  return true;
}

/**
 * @brief   Read the header line into a TagLogHeader.
 *
 * @param[in]  line    The first line of the file.
 * @param[out] header  Config, info, calibration and states.
 * @param[out] error   Why it failed.
 * @return  false when the line is not an object or `config`/`info` is
 *          missing or malformed.
 */
bool ParseHeader(const std::string &line, TagLogHeader &header, std::string &error) {
  google::protobuf::Struct root;
  const auto status = pbu::JsonStringToMessage(line, &root);
  if (!status.ok()) {
    error = "header line: " + std::string(status.message());
    return false;
  }
  for (const char *required : {"config", "info"}) {
    if (!root.fields().count(required)) {
      error = std::string("header has no ") + required;
      return false;
    }
  }
  return ParseMember(root.fields().at("config"), header.config, "config", error) &&
         ParseMember(root.fields().at("info"), header.info, "info", error) &&
         ParseList(root, "calibration", header.calibration, error) &&
         ParseList(root, "states", header.states, error);
}

/**
 * @brief   Write the import into @p db_path; the caller cleans up on failure.
 *
 * @param[in]  in        The input, positioned after the header line.
 * @param[in]  header    The parsed header.
 * @param[in]  input     Input path, recorded in the info table.
 * @param[in]  extra     KEY=VALUE pairs for the info table.
 * @param[in]  db_path   Output database, replaced.
 * @param[out] records   Records the writer reported consuming.
 * @param[out] acks      Ack lines written.
 * @param[out] error     Why it failed.
 * @return  false on a malformed line, a writer error or an Ack with no log
 *          payload for this tag type.
 */
bool WriteImport(std::istream &in, const TagLogHeader &header, const std::string &input,
                 const std::vector<std::string> &extra, const std::string &db_path,
                 uint64_t &records, uint64_t &acks, std::string &error) {
  SqliteTagLogWriter writer(db_path, header.config, true);
  if (!writer.isOpen() || !writer.writeHeader(header) ||
      !writer.writeInfo("source", "import") || !writer.writeInfo("import_file", input)) {
    error = writer.lastError();
    return false;
  }
  for (const auto &pair : extra) {
    const auto eq = pair.find('=');
    if (eq == std::string::npos || eq == 0) {
      error = "--info expects KEY=VALUE, got " + pair;
      return false;
    }
    if (!writer.writeInfo(pair.substr(0, eq), pair.substr(eq + 1))) {
      error = writer.lastError();
      return false;
    }
  }

  // tag-dwnld writes only the header when there is nothing to download, and
  // opens the log transaction only when there is.
  bool begun = false;
  std::string line;
  uint64_t line_number = 1;
  while (std::getline(in, line)) {
    line_number++;
    if (line.find_first_not_of(" \t\r") == std::string::npos)
      continue;
    Ack ack;
    const auto status = pbu::JsonStringToMessage(line, &ack);
    if (!status.ok()) {
      error = "line " + std::to_string(line_number) + ": " + std::string(status.message());
      return false;
    }
    if (!begun) {
      if (!writer.beginLog()) {
        error = writer.lastError();
        return false;
      }
      begun = true;
    }
    const int len = writer.writeLog(ack);
    if (len < 0) {
      error = "line " + std::to_string(line_number) + ": " + writer.lastError();
      return false;
    }
    // tag_dwnld.py writes only pages that carried records, so a line the
    // writer consumes nothing from is a mismatch with the header's tag type.
    if (len == 0) {
      error = "line " + std::to_string(line_number) +
              ": Ack carries no log payload for tag type " +
              TagType_Name(header.config.tag_type());
      return false;
    }
    records += static_cast<uint64_t>(len);
    acks++;
  }
  if (begun && !writer.endLog()) {
    error = writer.lastError();
    return false;
  }
  return true;
}

} // namespace

/**
 * @brief   Parse options and import.
 * @return  0 when the database was written, 1 otherwise.
 */
int main(int argc, char **argv) {
  cxxopts::Options options("tag-log-import",
                           "Write a tagclient JSONL download as a SQLite log");
  options.add_options()
      ("input", "JSONL download (tag_dwnld.py format)", cxxopts::value<std::string>())
      ("o,output", "SQLite file to write (replaced)", cxxopts::value<std::string>())
      ("info", "extra info-table row KEY=VALUE (repeatable)",
       cxxopts::value<std::vector<std::string>>())
      ("h,help", "Print usage");
  options.parse_positional({"input"});
  options.positional_help("<download.jsonl>");
  cxxopts::ParseResult args;
  try {
    args = options.parse(argc, argv);
  } catch (const std::exception &e) {
    std::cerr << e.what() << "\n" << options.help() << std::endl;
    return 1;
  }
  if (args.count("help") || !args.count("input") || !args.count("output")) {
    std::cout << options.help() << std::endl;
    return args.count("help") ? 0 : 1;
  }

  const std::string input = args["input"].as<std::string>();
  const std::string output = args["output"].as<std::string>();
  std::vector<std::string> extra;
  if (args.count("info"))
    extra = args["info"].as<std::vector<std::string>>();

  std::ifstream in(input);
  std::string line;
  if (!in || !std::getline(in, line)) {
    std::cerr << "tag-log-import: cannot read " << input << std::endl;
    return 1;
  }
  TagLogHeader header;
  std::string error;
  if (!ParseHeader(line, header, error)) {
    std::cerr << "tag-log-import: " << error << std::endl;
    return 1;
  }

  uint64_t records = 0;
  uint64_t acks = 0;
  if (!WriteImport(in, header, input, extra, output, records, acks, error)) {
    std::remove(output.c_str());
    std::cerr << "tag-log-import: " << error << std::endl;
    return 1;
  }
  std::cout << records << " records (" << acks << " pages) written to " << output
            << std::endl;
  return 0;
}
