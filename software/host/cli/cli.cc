#include "cli.h"
using namespace std::chrono_literals;
using namespace google::protobuf;

#ifdef _WIN64
#include <csignal>
#else
#include <signal.h>
#endif

extern bool parse_options(int argc, char **argv, cxxopts::Options &options, Tag &tag, UsbDev &dev);

static std::condition_variable cv;
static std::mutex cv_m;
//static int ticks = 0;

void keepalive(Tag &tag)
{
  std::unique_lock<std::mutex> lk(cv_m);
  while (std::cv_status::timeout ==
         cv.wait_until(lk, std::chrono::steady_clock::now() + 2s))
  {
    Status status;
    tag.GetStatus(status);
    //ticks++;
  }
  //std::cerr << "keepalive exiting" << std::endl;
}

void prompt()
{
  if (isatty(fileno(stdin)))
    std::cout << "> ";
}

static void intHandler(int dummy)
{
  (void)dummy;
  exit(1);
}

int main(int argc, char **argv)
{
  Tag tag;
  UsbDev dev;
  std::string input;

  cxxopts::Options options("tag-cli", "Executes commands from stdin");

  // Parse options

  if (parse_options(argc, argv, options, tag, dev) && tag.Attach(dev))
  {
    // print floating point as  xxxxx.yy

    std::cout << std::fixed;
    std::cout.precision(2);

    // catch ctl-c
    signal(SIGINT, intHandler);

    // start a keep alive thread

    std::thread ticker(keepalive, std::ref(tag));

    // process input

    for (prompt(); std::getline(std::cin, input); prompt())
    {
      std::stringstream line(input); // include <sstream>
      std::string token;
      if (line >> token)
      {
        cmd_eval(tag, token, line);
      }
    }
    cv.notify_all();
    ticker.join();
    //std::cerr << "Ticks: " << ticks << std::endl;
  }
}
