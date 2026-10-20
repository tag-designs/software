/**
 * @file    main.cc
 * @brief   tag-server: serve one tag base over gRPC for test programs.
 *
 * @details Parses the options, optionally attaches at start-up, listens on a
 *          Unix domain socket (the default on Linux and macOS) or a TCP
 *          address, keeps the monitor session alive while attached, and on
 *          SIGINT or SIGTERM stops serving, detaches and removes its socket.
 *          See README.md.
 */
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

#ifndef _WIN32
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#endif

#include <cxxopts.hpp>
#include <grpcxx/server.h>
#include <tagclass.h>

#include "tag_server.h"

extern "C"
{
#include "log.h"
}

namespace
{

std::atomic<bool> stop_requested{false};

void on_signal(int) { stop_requested = true; }

std::string default_listen()
{
#ifdef _WIN32
  return "127.0.0.1:50061";
#else
  return "unix:/tmp/tag-server-" + std::to_string(getuid()) + ".sock";
#endif
}

#ifndef _WIN32
/**
 * @brief   Binds a Unix domain socket at @p path for grpcxx to listen on.
 * @details Refuses a path where another server already accepts connections,
 *          removes a stale socket file otherwise, and makes the socket
 *          readable and writable by its owner only.
 * @return  The bound socket, or -1 after printing the reason.
 */
int bind_unix_socket(const std::string &path)
{
  sockaddr_un addr{};
  if (path.size() >= sizeof(addr.sun_path))
  {
    std::cerr << "socket path too long: " << path << std::endl;
    return -1;
  }
  addr.sun_family = AF_UNIX;
  std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);

  // Another server already there?
  int probe = ::socket(AF_UNIX, SOCK_STREAM, 0);
  if (probe >= 0)
  {
    const bool in_use = ::connect(probe, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) == 0;
    ::close(probe);
    if (in_use)
    {
      std::cerr << "another server is listening on " << path << std::endl;
      return -1;
    }
  }
  ::unlink(path.c_str());

  int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd < 0 || ::bind(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0)
  {
    std::perror(("bind " + path).c_str());
    if (fd >= 0)
      ::close(fd);
    return -1;
  }
  ::chmod(path.c_str(), S_IRUSR | S_IWUSR);
  return fd;
}
#endif

bool parse_base(const std::string &text, unsigned &bus, unsigned &address)
{
  return std::sscanf(text.c_str(), "%u:%u", &bus, &address) == 2;
}

} // namespace

int main(int argc, char **argv)
{
  std::string listen = default_listen();
  std::string base;
  int keepalive_s = 2;
  int threads = 2;

  cxxopts::Options options("tag-server",
                           "Serve a tag base over gRPC (tagrpc.TagService) for test programs");
  options.add_options()
    ("l,listen", "unix:<path> or <ip>:<port>", cxxopts::value<std::string>(listen))
    ("b,base", "attach at start-up to the base at bus:device", cxxopts::value<std::string>(base))
    ("a,attach", "attach at start-up to the only base")
    ("enable-debug", "serve memory read/write and the field-rescue calls")
    ("keepalive", "seconds between keepalives while attached", cxxopts::value<int>(keepalive_s))
    ("threads", "gRPC worker threads", cxxopts::value<int>(threads))
    ("v,verbose", "log at DEBUG level")
    ("h,help", "print usage");

  bool enable_debug = false;
  bool attach_only = false;
  try
  {
    auto result = options.parse(argc, argv);
    if (result.count("help"))
    {
      std::cout << options.help() << std::endl;
      return 0;
    }
    enable_debug = result.count("enable-debug") > 0;
    attach_only = result.count("attach") > 0;
    log_set_level(result.count("verbose") ? LOG_DEBUG : LOG_ERROR);
  }
  catch (const cxxopts::OptionException &e)
  {
    std::cerr << "error parsing options: " << e.what() << std::endl;
    return 1;
  }
  if (keepalive_s < 1 || threads < 1)
  {
    std::cerr << "--keepalive and --threads must be at least 1" << std::endl;
    return 1;
  }

  Tag tag;

  // Optional attach at start-up.
  if (!base.empty() || attach_only)
  {
    std::vector<UsbDev> devs;
    tag.Available(devs);
    UsbDev chosen;
    bool found = false;
    if (!base.empty())
    {
      unsigned bus = 0, address = 0;
      if (!parse_base(base, bus, address))
      {
        std::cerr << "--base must be bus:device" << std::endl;
        return 1;
      }
      for (const auto &dev : devs)
        if (dev.bus == bus && dev.address == address)
        {
          chosen = dev;
          found = true;
        }
    }
    else if (devs.size() == 1)
    {
      chosen = devs.front();
      found = true;
    }
    else if (devs.size() > 1)
    {
      std::cerr << "several bases found; use --base bus:device" << std::endl;
      return 1;
    }
    if (!found || !tag.Attach(chosen))
    {
      std::cerr << "attach failed" << std::endl;
      return 1;
    }
  }

  TagServer impl(tag, enable_debug);
  tagrpc::TagService::Service service(impl);
  grpcxx::server server(static_cast<std::size_t>(threads));
  server.add(service);

  std::signal(SIGINT, on_signal);
  std::signal(SIGTERM, on_signal);

  // Turn the signal flag into a stop request for grpcxx, and keep the monitor
  // session alive while attached.
  std::stop_source stop;
  std::thread housekeeping([&] {
    auto next_keepalive = std::chrono::steady_clock::now();
    while (!stop_requested)
    {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      if (std::chrono::steady_clock::now() >= next_keepalive)
      {
        if (tag.IsAttached())
          tag.KeepAlive();
        next_keepalive = std::chrono::steady_clock::now() + std::chrono::seconds(keepalive_s);
      }
    }
    stop.request_stop();
  });

  std::string socket_path;
  int rc = 0;
  try
  {
    if (listen.rfind("unix:", 0) == 0)
    {
#ifdef _WIN32
      std::cerr << "unix sockets are not supported on Windows; use <ip>:<port>" << std::endl;
      rc = 1;
#else
      socket_path = listen.substr(5);
      int fd = bind_unix_socket(socket_path);
      if (fd < 0)
        rc = 1;
      else
      {
        std::cout << "tag-server listening on unix:" << socket_path
                  << (enable_debug ? " (debug enabled)" : "") << std::endl;
        server.run(std::move(fd), stop.get_token());
      }
#endif
    }
    else
    {
      const auto colon = listen.rfind(':');
      if (colon == std::string::npos)
      {
        std::cerr << "--listen must be unix:<path> or <ip>:<port>" << std::endl;
        rc = 1;
      }
      else
      {
        const std::string ip = listen.substr(0, colon);
        const int port = std::atoi(listen.c_str() + colon + 1);
        std::cout << "tag-server listening on " << ip << ":" << port
                  << (enable_debug ? " (debug enabled)" : "") << std::endl;
        server.run(ip, port, stop.get_token());
      }
    }
  }
  catch (const std::exception &e)
  {
    std::cerr << "server error: " << e.what() << std::endl;
    rc = 1;
  }

  stop_requested = true;
  housekeeping.join();
  tag.Detach();
#ifndef _WIN32
  if (!socket_path.empty())
    ::unlink(socket_path.c_str());
#endif
  return rc;
}
