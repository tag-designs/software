/**
 * @file    tag-rebuild.cc
 * @brief   Rebuild a SQLite download from a tag-capture directory, without
 *          the tag (docs/decisions/0018-offline-rebuild-capture-backed-source.md).
 *
 * @details The capture's internal flash, backup registers and external flash
 *          are turned back into the messages the firmware would have served,
 *          and written by the same SQLite writer as tag-dwnld:
 *
 *              tag-rebuild captures/.../capture-20261002-152906 -o tag.db3
 *
 *          The result is "as captured": the tag as found, before any reset
 *          recovery a live attach would run. Its info table records
 *          source = capture, the capture directory and the capture time.
 *          Families whose decoder is not implemented are refused.
 *
 *          Exit status: 0 written, 1 not.
 *
 * @see     host/libraries/tagcore/recovery/capturesource.h
 */

#include <iostream>
#include <string>

#include <cxxopts.hpp>

#include "recovery/capturesource.h"

/**
 * @brief   Parse options and rebuild.
 * @return  0 when the database was written, 1 otherwise.
 */
int main(int argc, char **argv) {
  cxxopts::Options options("tag-rebuild",
                           "Rebuild a SQLite download from a tag-capture directory");
  options.add_options()
      ("capture", "tag-capture directory", cxxopts::value<std::string>())
      ("o,output", "SQLite file to write (replaced)", cxxopts::value<std::string>())
      ("h,help", "Print usage");
  options.parse_positional({"capture"});
  options.positional_help("<capture-dir>");
  cxxopts::ParseResult args;
  try {
    args = options.parse(argc, argv);
  } catch (const std::exception &e) {
    std::cerr << e.what() << "\n" << options.help() << std::endl;
    return 1;
  }
  if (args.count("help") || !args.count("capture") || !args.count("output")) {
    std::cout << options.help() << std::endl;
    return args.count("help") ? 0 : 1;
  }

  uint32_t records = 0;
  std::string error;
  if (!tagcore::recovery::RebuildSqliteFromCapture(
          args["capture"].as<std::string>(), args["output"].as<std::string>(),
          &records, &error)) {
    std::cerr << "tag-rebuild: " << error << std::endl;
    return 1;
  }
  std::cout << records << " records written to " << args["output"].as<std::string>()
            << std::endl;
  return 0;
}
