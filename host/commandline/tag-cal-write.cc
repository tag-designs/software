#include <iostream>

#include <tag.pb.h>
#include <tagclass.h>
#include <cxxopts.hpp>

extern bool parse_options(int argc, char **argv, cxxopts::Options &options, Tag &tag, UsbDev &dev);

/*
 * Writes an identity (no-op) magnetometer calibration constant set:
 * M' = A(M-V) with A = identity, V = 0, B = 1. Several CompassTag-family
 * state handlers refuse to start a run until sensorsHaveCalibration() sees
 * at least one stored entry ("Device must be calibrated", config.c), which a
 * mass-erase flash (STM32_Programmer_CLI -e all) wipes along with
 * everything else. This unblocks power/lifecycle testing on a freshly
 * reflashed board without requiring a physical rotation-based calibration
 * pass. Magnetometer readings taken under it are NOT calibrated -- run
 * tag-cal (interactive) or qtcalibrate for real calibration before trusting
 * compass output from a board this was used on.
 */
int main(int argc, char **argv)
{
    Tag tag;
    UsbDev dev;

    cxxopts::Options options("tag-cal-write",
                             "write an identity (no-op) calibration constant set, "
                             "to unblock a tag that refuses to start with "
                             "\"Device must be calibrated\" after a mass-erase "
                             "reflash -- NOT a substitute for real calibration");

    if (parse_options(argc, argv, options, tag, dev) && tag.Attach(dev))
    {
        CalibrationConstants constants;
        constants.set_timestamp(0);
        CalibrationConstants::MagConstants *m = constants.mutable_magnetometer();
        m->set_b(1.0f);
        m->set_v0(0.0f);
        m->set_v1(0.0f);
        m->set_v2(0.0f);
        m->set_a00(1.0f);
        m->set_a01(0.0f);
        m->set_a02(0.0f);
        m->set_a10(0.0f);
        m->set_a11(1.0f);
        m->set_a12(0.0f);
        m->set_a20(0.0f);
        m->set_a21(0.0f);
        m->set_a22(1.0f);

        if (!tag.WriteCalibration(constants))
        {
            std::cerr << "WriteCalibration failed: " << tag.DebugMessage() << std::endl;
            return 1;
        }
        std::cout << "wrote identity calibration constants "
                     "(placeholder only -- not a real calibration; "
                     "compass output will not be accurate until tag-cal/"
                     "qtcalibrate is run)" << std::endl;
    }
    else
    {
        std::cerr << "Attach failed" << std::endl;
        return 1;
    }

    return 0;
}
