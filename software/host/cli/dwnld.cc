#include "dwnld.h"
#include <taglogs.h>
#include <time.h>

using namespace std::chrono_literals;
using namespace google::protobuf;

#ifdef _WIN64
#include <csignal>
#else
#include <signal.h>
#endif

extern bool parse_options(int argc, char **argv, cxxopts::Options &options, Tag &tag, UsbDev &dev);
std::string fname;
bool stoptag;

static std::condition_variable cv;
static std::mutex cv_m;
//static int ticks = 0;

static void intHandler(int dummy)
{
  (void)dummy;
  exit(1);
}

clock_t elapsed = 0;
int downloads = 0;

int main(int argc, char **argv)
{
  Tag tag;
  UsbDev dev;
  std::string input;
  Status s;
  clock_t start,end;

  cxxopts::Options options("tag-dwnld", "Downloads data from file");

  // Parse options

  if (parse_options(argc, argv, options, tag, dev) && tag.Attach(dev))
  {
    // print floating point as  xxxxx.yy

    std::cout << std::fixed;
    std::cout.precision(2);

    // catch ctl-c
    signal(SIGINT, intHandler);

    Status s;

    while (1)
    {
      if (!tag.GetStatus(s))
      {
        break;
      }

      if ((s.state() == RUNNING) && stoptag)
      {
        tag.Stop();
        tag.GetStatus(s);
      }

      if ((s.state() != FINISHED) && (s.state() != ABORTED))
      {
        std::string formatted;
        TextFormat::PrintToString(s, &formatted);
        std::cerr << "Can't dump logs from current state \n";
        std::cerr << formatted << std::endl;
        break;
      }

      dumpTagLogHeader(std::cout, tag, tag_log_output_txt);
      int len = 0;
      int total = 0;
      Config config;
      tag.GetConfig(config);
      Ack ack;
      do
      {
        ack.Clear();
        len = 0;
        bool ret;

  
        ret = tag.GetDataLog(ack,total);
   
    
        downloads++;
        if (ret)
        {
          start = clock();
          //std::cerr << ack.DebugString() << "\n";
          len = dumpTagLog(std::cout, ack, config, tag_log_output_txt);
          total += len;
          end = clock();
          elapsed += (end-start);
        }
      } while (len);
      std::cerr << "Downloaded " << total << " logs\n";
      std::cerr << "Decoding time"  << (((double) elapsed)/CLOCKS_PER_SEC) << " seconds\n";
      break;
    }
    std::cerr << "Exit\n";
  }
}
