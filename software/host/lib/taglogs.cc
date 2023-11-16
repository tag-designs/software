
#include <string>
#include <iostream>
#include <list>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#include <tag.pb.h>
#include <tagdata.pb.h>
//#include <host.pb.h>
#include <tagclass.h>
#include <taglogs.h>
#include <log.h>
#include <chrono>

static std::string fmthex(uint32_t num)
{
  const char *digits = "0123456789ABCDEF";
  std::string str = "0x";
  for (int i = 32; i; i = i - 4)
  {
    str += digits[(num >> (i - 4)) & 0xf];
  }
  return str;
}

bool dumpTagLogHeader(std::ostream &fs, Tag &tag, enum TagLogOutput format)
{
  Config cfg;
  Status status;
  StateLog state_log;
  std::string str;
  TagInfo info;
  int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
  //QDateTime now = QDateTime::currentDateTime();

  if (!tag.GetStatus(status))
    return false;
  if (!tag.GetConfig(cfg))
    return false;

  tag.GetTagInfo(info);

  int64_t timeerr = status.millis() - now;
  fs << "#\n# Tag Read time: " << now / 1000 << "\n";
  fs << "#\n# Tag Error:     " << timeerr / 1000.0 << "\n";

  // Dump tag info

  fs << "#\n#             Tag Info\n#\n";
  fs << "# Tag Type:    " << TagType_Name(info.tag_type()) << "\n";
  fs << "# Tag Firmware:" << info.firmware() << "\n";
  fs << "# Board:       " << info.board_desc() << "\n";
  fs << "# Build Date:  " << info.build_time() << "\n";
  fs << "# Git Repo:    " << info.gitrepo() << "\n";
  fs << "# Source Path: " << info.source_path() << "\n";
  fs << "# Git Hash:    " << info.githash() << "\n";
  fs << "# UUID:        " << info.uuid() << "\n";
  fs << "# Internal Flash Size:  " << info.intflashsz() << "KB"
     << "\n";
  fs << "# External Flash Size:  " << info.extflashsz() << "KB"
     << "\n";

  TestResult result = status.test_status();

  fs << "# Test Status: " << TestResult_Name(result) << "\n";
  fs << "#\n";

  // Dump Configuration -- need to handle
  //                       alternative configurations
  //                       this should be part of the config form

  fs << "#             Configuration\n#\n";
  if (cfg.has_adxl362())
  {
    fs << "# Range:       "
       << Adxl362_Rng_Name(cfg.adxl362().range()) << "\n";
    fs << "# Frequency:   "
       << Adxl362_Odr_Name(cfg.adxl362().freq()) << "\n";
    fs << "# Filter:      "
       << Adxl362_Aa_Name(cfg.adxl362().filter()) << "\n";
    fs << "# Active Thresh: " << cfg.adxl362().act_thresh_g() << "g"
       << "\n";
    fs << "# Inactive Thresh: " << cfg.adxl362().inact_thresh_g() << "g"
       << "\n";
    fs << "# thresh Time: " << cfg.adxl362().inactive_sec() << "s"
       << "\n";
  }

  fs << "# Start:       " << cfg.active_interval().start_epoch() << "\n";
  fs << "# End:         " << cfg.active_interval().end_epoch() << "\n";

  if (cfg.bittag_log() != BITTAG_UNSPECIFIED)
  {
    fs << "# BitTag Log Format:      "
       << cfg.bittag_log() << "\n";
  }

  if (cfg.period())
  {
    //fs << "# Pressure log internal:    " << cfg.prestag_log().internal() << std::endl;
    fs << "# Sample Period:   "
       << cfg.period() << " seconds\n";
  }

  for (int i = 0; i < cfg.hibernate_size(); i++)
  {
    fs << "# Hibernate[" << i << "] = ";
    fs << "{" << cfg.hibernate(i).start_epoch() << ", " << cfg.hibernate(i).end_epoch() << "}\n";
  }
  fs << "#\n";

  // Dump State Log

  fs << "#             State Log\n#\n";

  // std::list<State>::const_iterator iterator;
  // for (iterator = state_log.begin(); iterator != state_log.end(); ++iterator)
  // {

  int next = 0;
  while (tag.GetStateLog(state_log, next))
  {
    next += state_log.states().size();
    for (auto const &state : state_log.states())
    {
      fs << "# Epoch: " << state.status().millis() / 1000 << "\n";
      fs << "# State: " << TagState_Name(state.status().state()) << "\n";
      fs << "# Entry Code: " << State_Event_Name(state.transition_reason()) << "\n";
      fs << "# Temperature: " << state.status().temperature() << "\n";
      fs << "# Voltage:     " << state.status().voltage() << "\n";
      fs << "# Internal Log Entries: " << state.status().internal_data_count() << "\n";
      fs << "# External Log Entries: " << state.status().external_data_count() << "\n";
      fs << "#\n";
    }
  }

  fs << "#\n";
  fs << "#             Data Log\n#\n";
  return true;
}

static int dumpTagLog(std::ostream &out, /*Tag &t,*/
                      const BitTagLog &log,
                      enum BitTagLogFmt dataformat,
                      enum TagLogOutput format)
{
  BitTagData entry;
  int count = 0;

  int bucket_bits, bucket_number, bucket_period;
  const char *bucket_name;

  switch (dataformat)
  {
  case BITTAG_BITPERSEC: // special case below
    break;
  case BITTAG_BITSPERMIN:
    bucket_bits = 6;
    bucket_number = 10;
    bucket_period = 60;
    bucket_name = "MIN:";
    break;
  case BITTAG_BITSPERFOURMIN:
    bucket_bits = 8;
    bucket_number = 8;
    bucket_period = 60 * 4;
    bucket_name = "FOURMIN:";
    break;
  case BITTAG_BITSPERFIVEMIN:
    bucket_bits = 9;
    bucket_number = 7;
    bucket_period = 60 * 5;
    bucket_name = "FIVEMIN:";
    break;
  default:
    return 0;
  }

  for (auto const &entry : log.data())
  /* for (std::vector<BitTagData>::iterator entry = log.begin();
       entry != log.end(); ++entry) */
  {
    uint64_t rawdata = entry.rawdata();
    if (dataformat != BITTAG_BITPERSEC)
    {
      for (int i = 0; i < bucket_number; i++)
      {
        int64_t timestamp =
            entry.epoch() - bucket_period * (bucket_number - 1 - i);
        uint64_t count =
            (rawdata >> (i * bucket_bits)) & ((1 << bucket_bits) - 1);
        out << timestamp << ",";
        out << bucket_name
            << (count * 100.0 / bucket_period)
            << std::endl;
      }
    }
    else
    {
      // split into two 30 bit segments for consistency with
      // old form.  Newest bit 59, oldest bit is 0

      uint32_t high = (rawdata >> 30) & 0x3fffffff;
      uint32_t low = (rawdata)&0x3fffffff;
      int64_t timestamp = entry.epoch();
      out << timestamp - 30 << ",";
      //fs << QString("SEC:0x%1").arg(low, 8, 16, QChar('0')).toStdString() << "\n";
      out << "SEC:" << fmthex(low) << std::endl;
      out << timestamp << ",";
      //fs << hex << std::showbase << std::internal << std::setfill('0') << std::setw(8);
      //fs << QString("SEC:0x%1").arg(high, 8, 16, QChar('0')).toStdString() << "\n";
      out << "SEC:" << fmthex(high) << std::endl;
    }
    out << entry.epoch() << ",";
    out << "V:" << entry.voltage();
    out << ",TC:" << entry.temperature() << std::endl;

    count++;
  }
  return count;
}

static int dumpTagLog(std::ostream &out, const PresTagLog &log,
                      uint32_t period,
                      enum TagLogOutput format)
{
  int64_t timestamp = log.epoch();
  out << timestamp << ",";
  out << "V:" << log.voltage();
  out << ",TC:" << log.temperature() << std::endl;

  for (auto const &entry : log.data())
  {
    out << timestamp << ",";
    out << "P:" << entry.pressure() << std::endl;
    out << timestamp << ",";
    out << "T:" << entry.temperature() << std::endl;
    timestamp += period;
  }
  return 1;
}

static int dumpTagLog(std::ostream &out, const BitTagNgLog &log,
                      enum TagLogOutput format)
{
  int64_t timestamp = log.epoch();
  out << timestamp << ",";
  out << "V:" << log.voltage();
  out << ",TC:" << log.temperature() << std::endl;

  for (auto const &activity : log.activity())
  {
      // unpack data
      // data start 120 seconds (4 30 second blocks) before the header
    for (int i = 0; i < 4; i++) {
      out << timestamp-120 << ",";
      out << "A:" << ((activity>>(i*8))&0xff)/0.30 << std::endl;
      timestamp += 30;
    }
  }
  return 1;
}

static int dumpTagLog(std::ostream &out, const LuxTagLog &log,
                      uint32_t period,
                      enum TagLogOutput format)
{
  int64_t timestamp = log.epoch();
  out << timestamp << ",";
  out << "V:" << log.voltage();
  out << ",TC:" << log.temperature() << std::endl;

  for (auto const &entry : log.data())
  {
    out << timestamp << ",";
    out << "Lx:" << entry.lux() << std::endl;
    timestamp += period;
  }
  return 1;
}

static int dumpTagLog(std::ostream &out, const AccelTagLog &log,
                      uint32_t period,
                      enum TagLogOutput format)
{
  int64_t timestamp = log.epoch();
  out << timestamp << ",";
  out << "V:" << log.voltage();
  out << ",TC:" << log.temperature() << std::endl;

  for (auto const &entry : log.data())
  {
    out << timestamp << ",";
    out << "PITCH:" << entry.pitch() << std::endl;
    out << "PWR:" << entry.power() << std::endl;
    timestamp += period;
  }
  return 1;
}

static int dumpTagLog(std::ostream &out, const GeoTagLog &log)
{
  int i;

  // Header

  int64_t timestamp = log.epoch();
  out << timestamp << ",";
  out << "V:" << log.voltage();
  out << ",TC:" << log.temperature() << std::endl;

  // Compass data

  out << timestamp << ",";
  out << "A:" << log.compass().ax() << ",";
  out << log.compass().ay() << ",";
  out << log.compass().az() << std::endl;
  out << timestamp << ",";
  out << "M:" << log.compass().mx() << ",";
  out << log.compass().my() << ",";
  out << log.compass().mz() << std::endl;

  // loop over the data

  for (int i = 0; i < log.optpwr_size(); i++)
  {
    out << timestamp << ",";
    out << "Lx:" << log.optpwr(i).lux() << std::endl;
    out << timestamp << ",";
    out << "Pwr:" << log.optpwr(i).pwr() << std::endl;
    out << timestamp << ",";
    out << "Pitch:" << log.optpwr(i).pitch() << std::endl;
    timestamp += 60;
  }
  return 1;
}

int dumpTagLog(std::ostream &out,
               const Ack &log,
               const Config &config,
               enum TagLogOutput format)
{

  switch (config.tag_type())
  {
  case BITTAG:
    if (log.has_bittag_data_log())
    {
      return dumpTagLog(out, log.bittag_data_log(),
                        config.bittag_log(), format);
    }
    break;
  case BITTAGNG:
    if (log.has_bittag_ng_data_log())
    {
      return dumpTagLog(out, log.bittag_ng_data_log(), format);
    }
    break;
  case ACCELTAG:
    if (log.has_acceltag_data_log())
    {
      return dumpTagLog(out, log.acceltag_data_log(), config.period(), format);
    }
    break;
  case PRESTAG:
    if (log.has_prestag_data_log())
    {
      return dumpTagLog(out, log.prestag_data_log(), config.period(), format);
    }
    break;
  case LUXTAG:
    if (log.has_luxtag_data_log())
    {
      return dumpTagLog(out, log.luxtag_data_log(), config.period(), format);
    }
    break;
  case GEOTAG:
    if (log.has_geotag_data_log())
    {
      return dumpTagLog(out, log.geotag_data_log());
    }
    break;
  default:
    return -1;
  }
  return 0;
}
