#include <stdint.h>
#include <sys/time.h>
#include <unistd.h>
#include <iomanip>
#include <iostream>
#include <fstream>
#include <list>
#include <mutex>
#include <string>
#include <vector>

#include <tagdata.pb.h>
#include <tagclass.h>

using namespace google::protobuf;

bool rtcDrift(Tag &tag, float &f) {
  struct timeval tvBegin;
  double start, end;
  int64_t millis;
  gettimeofday(&tvBegin, NULL);
  if (tag.GetRtc(millis)) {
    start = tvBegin.tv_sec + 0.000001 * tvBegin.tv_usec;
    end = 0.001 * millis;
    f = end - start;
    return true;
  }
  return false;
}

static void intHandler(int dummy) {
  (void)dummy;
  exit(1);
}

int main(int argc, char **argv) {
  Tag tag;
  std::string str;
  int size;
  float f;
  int epoch;
  int millis;
  std::list<State> state_log;

  std::string uuid;
  std::ofstream outfile;

  // print floating point as  xxxxx.yyy

  std::cout << std::fixed;
  std::cout.precision(2);

  // catch ctl-c

  signal(SIGINT, intHandler);

  //  debug_level = LOG_LVL_DEBUG;

  if (tag.Attach()) {
    float tv = 0;

    // Read Tag Information

    if (tag.BoardName(str)) std::cout << "Board name: " << str << std::endl;
    if (tag.BuildTimeDate(str))
      std::cout << "Build time and date: " << str << std::endl;
    int cid;
    if (tag.FlashSize(size))
      std::cout << "Flash size: " << size << "kb" << std::endl;
    if (tag.RepoUrl(str)) std::cout << "Repo: " << str << std::endl;
    if (tag.RepoHash(str)) std::cout << "Repo hash: " << str << std::endl;
    if (tag.Uuid(str)){
      std::cout << "UUID: " << str << std::endl;
      uuid = str;
    }
    if (tag.Voltage(f)) std::cout << "Voltage: " << f << std::endl;

    // Read RTC

    //tag.GetRtc(millis);
    if (rtcDrift(tag, f)) {
      std::cout << "Initial Clock drift: " << f << std::endl;
    }
    std::cout << "#  Checking RTC (2 second delay)" << std::endl;
    tag.SetRtc();
    sleep(2);
    if (rtcDrift(tag, f)) {
      std::cout << "Clock drift after setting: " << f << std::endl;
    }

    // Read state

    State state;
    int pages;
    if (tag.GetState(state, str)) {
      std::cout << str;
      std::cout << "State: " << TagState_Name(state.state()) << std::endl;
    }

    if (state.state() != IDLE){
      std::cout << "#  State not idle ! Exiting" << std::endl;
    }

    // Run Test
    
    std::cout << "#  Running test" << std::endl;
    tag.Test();
    for (int i = 0; i < 5; i++) {
      tag.GetState(state,str);
      if (state.state() == IDLE)
        break;
      sleep(1);
    }

  tag.TestStatus(str);
  std::cout << "Test Result: " << str << std::endl;
  } else {
    std::cout << "Attach failed" << std::endl;
  }
  
  // Write UUID plus tag number to file
  if(!uuid.empty() && argc == 3){
    outfile.open(argv[1], std::ios_base::app);
    outfile << argv[2] << "," << uuid << std::endl;
    outfile.close();
    std::cout << "UUID recorded." << std::endl;
  }
  return 0;
}
