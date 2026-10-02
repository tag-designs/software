/**
 * @file    tag-sramcall.cc
 * @brief   Call one function of an SRAM-resident image on a halted tag and
 *          print the transfer buffer.
 *
 * @details Halts the tag at its reset vector (as tag-capture does), downloads
 *          an image built with the loader framework into SRAM1, calls the named
 *          function with the given arguments, and hex-dumps the first bytes of
 *          the transfer buffer. An argument written `buf` is replaced by the
 *          buffer's SRAM address. The tag is then reset and boots normally.
 *
 *          For probes such as embedded/loaders/RV3028_PresTagv3, which reads
 *          the RTC's registers without the tag's firmware:
 *
 *              tag-sramcall --image RV3028_PresTagv3.elf \
 *                  --call Rv3028ReadRegs --args buf,0,64 --dump 64
 *
 *          `--peek ADDR,LEN` also prints LEN bytes at ADDR, as 32-bit words,
 *          while the core is still halted, for example an image's static
 *          diagnostics found with `nm`.
 *
 *          Exit status: 0 the function returned 1, 2 it returned something
 *          else, 1 the call could not be made.
 *
 * @warning The image overwrites the start of SRAM1. Run tag-capture first if
 *          SRAM matters.
 *
 * @see     host/libraries/tagcore/recovery/sramcall.h
 */

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include <cxxopts.hpp>

extern "C" {
#include "log.h"
}

#include "recovery/sramcall.h"
#include "recovery/swdsession.h"
#include "recovery/targetimage.h"

using namespace tagcore::recovery;

/**
 * @brief   Parse options, make the call and print the result.
 * @return  0 when the function returned 1, 2 for any other result, 1 on
 *          failure to call.
 */
int main(int argc, char **argv) {
  cxxopts::Options options("tag-sramcall",
                           "Call a function of an SRAM image on a halted tag");
  options.add_options()
      ("i,image", "Image (.elf or .stldr)", cxxopts::value<std::string>())
      ("c,call", "Function to call", cxxopts::value<std::string>())
      ("a,args", "Comma-separated arguments; 'buf' is the buffer address",
       cxxopts::value<std::string>()->default_value(""))
      ("dump", "Bytes of the buffer to print after the call",
       cxxopts::value<uint32_t>()->default_value("0"))
      ("peek", "After the call, also print LEN bytes at ADDR (ADDR,LEN), for "
               "example an image's static diagnostics",
       cxxopts::value<std::string>()->default_value(""))
      ("timeout", "Milliseconds to wait for the return",
       cxxopts::value<int>()->default_value("2000"))
      ("d,debug", "Set log level to DEBUG")
      ("h,help", "Print usage");

  std::string image_path, symbol, args_text, peek_text;
  uint32_t dump = 0;
  int timeout_ms = 2000;
  try {
    auto result = options.parse(argc, argv);
    if (result.count("help") || !result.count("image") || !result.count("call")) {
      std::cout << options.help() << std::endl;
      return result.count("help") ? 0 : 1;
    }
    log_set_level(result.count("debug") ? LOG_DEBUG : LOG_ERROR);
    image_path = result["image"].as<std::string>();
    symbol = result["call"].as<std::string>();
    args_text = result["args"].as<std::string>();
    dump = result["dump"].as<uint32_t>();
    timeout_ms = result["timeout"].as<int>();
    peek_text = result["peek"].as<std::string>();
  } catch (const cxxopts::OptionException &e) {
    std::cerr << "error parsing options: " << e.what() << std::endl;
    return 1;
  }

  TargetImage image;
  std::string err;
  if (!image.Load(image_path, &err)) {
    std::cerr << err << std::endl;
    return 1;
  }

  SwdSession s;
  if (!s.Open()) {
    std::cerr << "could not halt the tag at its reset vector" << std::endl;
    return 1;
  }
  std::printf("%s halted at reset vector 0x%08X (firmware did not run)\n",
              s.Mcu()->name, s.AttachInfo().pc);

  SramCall call(s);
  if (!call.Download(image, &err)) {
    std::cerr << "download failed: " << err << std::endl;
    s.Close(SwdExit::HardwareReset);
    return 1;
  }
  if (dump > call.BufferSize())
    dump = call.BufferSize();

  std::vector<uint32_t> args;
  std::stringstream ss(args_text);
  for (std::string tok; std::getline(ss, tok, ',');) {
    if (tok.empty())
      continue;
    args.push_back(tok == "buf" ? call.BufferAddress()
                                : static_cast<uint32_t>(std::strtoul(tok.c_str(), nullptr, 0)));
  }
  if (args.size() > 4) {
    std::cerr << "at most four arguments" << std::endl;
    s.Close(SwdExit::HardwareReset);
    return 1;
  }
  while (args.size() < 4)
    args.push_back(0);

  // Clear the buffer, so bytes the function did not write read as 0xFF.
  std::vector<uint8_t> fill((dump + 3) & ~3U, 0xFF);
  if (!fill.empty())
    s.Write(call.BufferAddress(), fill.data(), fill.size());

  uint32_t r0 = 0;
  const bool ok = call.Call(symbol, {args[0], args[1], args[2], args[3]}, r0,
                            timeout_ms, &err);
  if (!ok) {
    std::cerr << "call failed: " << err << std::endl;
    s.Close(SwdExit::HardwareReset);
    return 1;
  }
  std::printf("%s returned %d (0x%08X)\n", symbol.c_str(),
              static_cast<int32_t>(r0), r0);

  if (dump) {
    std::vector<uint8_t> buf((dump + 3) & ~3U);
    if (!s.Read(call.BufferAddress(), buf.data(), buf.size(), 4096, nullptr)) {
      std::cerr << "SWD read of the buffer failed" << std::endl;
      s.Close(SwdExit::HardwareReset);
      return 1;
    }
    for (uint32_t i = 0; i < dump; i += 16) {
      std::printf("  %02X:", i);
      for (uint32_t j = i; j < i + 16 && j < dump; j++)
        std::printf(" %02X", buf[j]);
      std::printf("\n");
    }
  }
  if (!peek_text.empty()) {
    const size_t comma = peek_text.find(',');
    const uint32_t addr =
        static_cast<uint32_t>(std::strtoul(peek_text.c_str(), nullptr, 0));
    uint32_t len = comma == std::string::npos
        ? 16U
        : static_cast<uint32_t>(std::strtoul(peek_text.c_str() + comma + 1, nullptr, 0));
    len = (len + 3U) & ~3U;
    std::vector<uint8_t> buf(len);
    if (!s.Read(addr & ~3U, buf.data(), len, 4096, nullptr)) {
      std::cerr << "SWD read of the peek range failed" << std::endl;
    } else {
      std::printf("peek 0x%08X:\n", addr & ~3U);
      for (uint32_t i = 0; i < len; i += 4)
        std::printf("  +%02X: 0x%08X\n", i,
                    buf[i] | (buf[i + 1] << 8) | (buf[i + 2] << 16) |
                        (static_cast<uint32_t>(buf[i + 3]) << 24));
    }
  }
  s.Close(SwdExit::HardwareReset);
  return r0 == 1 ? 0 : 2;
}
