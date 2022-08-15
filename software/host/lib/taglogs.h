#ifndef TAG_LOGS_H
#define TAG_LOGS_H
#include "tag.pb.h"

enum TagLogOutput
{
    tag_log_output_txt,
    tag_log_output_json
};

// Single procedure for dumping general tag information and state

bool dumpTagLogHeader(std::ostream &out,
                      Tag &t,
                      enum TagLogOutput format);

// Type specific procedure for different log types
// This one is for BitTag data logs
// returns non-zero until it's done -- repeated calls are expected.

int dumpTagLog(std::ostream &out,
                const Ack &log,
                const Config &config,
                enum TagLogOutput format);

/*
int dumpTagLog(std::ostream &out,
               const AccelTagLog &log,
               uint32_t period,
               enum TagLogOutput format);

int dumpTagLog(std::ostream &out,
               const BitTagLog &log,
               enum BitTagLogFmt dataformat,
               enum TagLogOutput format);

int dumpTagLog(std::ostream &out,
                const LuxTagLog &log,
                uint32_t period,
                enum TagLogOutput format);

int dumpTagLog(std::ostream &out,
                const PresTagLog &log,
                uint32_t period,
                enum TagLogOutput format);

int dumpTagLog(std::ostream &out,
                const GeoTagLog &log);
*/
#endif /* TAG_LOGS_H */