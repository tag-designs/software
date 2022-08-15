#include <string>
#include <sstream>
#include <iostream>
#include <map>
#include <functional>
#include <stdio.h>
#ifdef WIN32
#include <io.h>
#define isatty(x) _isatty(x)
#else
#include <unistd.h>
#endif
#include <fcntl.h>
#include <google/protobuf/message.h>
#include <google/protobuf/text_format.h>
#include <regex>
#include <thread>
#include <condition_variable>
#include <mutex>
#include <chrono>
#include "tag.pb.h"
#include <tagclass.h>
#include <cxxopts.hpp>

extern bool cmd_eval(Tag &tag, std::string &name, std::stringstream &args);