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


#endif /* TAG_LOGS_H */