#include <stdint.h>
#include <string>
#include <vector>
#include <chrono>
#include <thread>

#include <tag.pb.h>
#include <tagclass.h>
#include <cxxopts.hpp>

#ifdef _WIN64
#include <csignal>
#else
#include <signal.h>
#endif

extern "C"
{
#include "log.h"
}



extern bool parse_options(int argc, char **argv, cxxopts::Options &options, Tag &tag, UsbDev &dev);
using namespace google::protobuf;
using MS = std::chrono::milliseconds;

int execute = 1;

static void intHandler(int dummy)
{
    (void)dummy;
    execute = 0;
    //exit(1);
}

int main(int argc, char **argv)
{
    
    UsbDev dev;
    Tag tag;

    cxxopts::Options options("tag-calibrate", "starts calibration mode");

    // Parse options

    if (parse_options(argc, argv, options, tag, dev) && tag.Attach(dev))
    {
        // print floating point as  xxxxx.yy
        std::cout << std::fixed;
        std::cout.precision(2);

        // catch ctl-c
        signal(SIGINT, intHandler);

        // read tag information

        Status status;
        tag.GetStatus(status);
        if (status.state() == IDLE)
        {
            // set the clock

            tag.SetRtc();

            // start calibration


            tag.Calibrate();
            while (execute) {
                tag.GetStatus(status);
                if (status.has_sensors()) {
                    SensorData sdata = status.sensors();
                    std::cout << "Raw:";
                    if (sdata.has_accel()){
                        std::cout << ((int) sdata.accel().ax()) << ",";
                        std::cout << ((int) sdata.accel().ay()) << ",";
                        std::cout << ((int) sdata.accel().az()) << ",";
                    } else {
                        std::cout << "0,0,0,";
                    }
                    std::cout << "0,0,0,";
                    if (sdata.has_mag()){
                        std::cout << ((int) (sdata.mag().mx()*10.0)) << ",";
                        std::cout << ((int) (sdata.mag().my()*10.0)) << ",";
                        std::cout << ((int) (sdata.mag().mz()*10.0));
                    } else {
                        std::cout << "0,0,0";
                    }
                    std::cout << "\r\n";

                    //std::cout << "Sensors: " << status.sensors().DebugString() << std::endl;   
                }
              
                //std::this_thread::sleep_for(MS(50));

            }
            tag.Stop();

        } else {
            std::cerr << "Tag not idle" << std::endl;
        }

   
    }
    else
    {
        std::cerr << "Attach failed" << std::endl;
    }

    return 0;
}
