/**
 * @file    flashplan.cpp
 * @brief   ELF symbol reading and the erase rule for qtprogram.
 */

#include "flashplan.h"

#include <QFile>
#include <QHash>
#include <QObject>

#include <cstring>

namespace
{

/// Every tag in this tree boots from the start of internal flash.
constexpr quint32 kFlashBase = 0x08000000u;

/// Little-endian 32-bit word at byte offset @p at; the caller has checked the bounds.
quint32 read32(const QByteArray &d, qint64 at)
{
  quint32 v;
  std::memcpy(&v, d.constData() + at, 4);
  return v; // ELF is little-endian, as are the hosts this runs on.
}

/// Little-endian 16-bit half-word at byte offset @p at; the caller has checked the bounds.
quint16 read16(const QByteArray &d, qint64 at)
{
  quint16 v;
  std::memcpy(&v, d.constData() + at, 2);
  return v;
}

/**
 * @brief   Reads symbol values from a 32-bit little-endian ELF.
 * @param[in]  data   The whole file.
 * @param[in]  wanted Symbol names to return.
 * @param[out] error  Set when the file is not such an ELF or is truncated.
 * @return  Name to value for each wanted symbol found.
 */
QHash<QString, quint32> elfSymbols(const QByteArray &data, const QStringList &wanted,
                                   QString &error)
{
  QHash<QString, quint32> out;
  if (data.size() < 0x34 || !data.startsWith("\x7f" "ELF") || data[4] != 1 || data[5] != 1) {
    error = QObject::tr("not a 32-bit little-endian ELF file");
    return out;
  }
  const qint64 shoff = read32(data, 0x20);
  const qint64 shentsize = read16(data, 0x2E);
  const qint64 shnum = read16(data, 0x30);
  if (shentsize < 40 || shoff + shnum * shentsize > data.size()) {
    error = QObject::tr("truncated ELF section table");
    return out;
  }
  auto section = [&](qint64 i, int field) { return read32(data, shoff + i * shentsize + field * 4); };
  for (qint64 i = 0; i < shnum; ++i) {
    if (section(i, 1) != 2) // SHT_SYMTAB
      continue;
    const qint64 offset = section(i, 4), size = section(i, 5), entsize = section(i, 9);
    const qint64 link = section(i, 6);
    if (entsize < 16 || link >= shnum || offset + size > data.size())
      continue;
    const qint64 strtab = section(link, 4);
    for (qint64 pos = offset; pos + 8 <= offset + size; pos += entsize) {
      const qint64 name = strtab + read32(data, pos);
      if (name >= data.size())
        continue;
      const QString symbol = QString::fromLatin1(data.constData() + name);
      if (wanted.contains(symbol))
        out.insert(symbol, read32(data, pos + 4));
    }
  }
  return out;
}

} // namespace

QStringList FlashPlan::eraseArgs() const
{
  switch (erase) {
  case Erase::BelowCalibration:
    return {"-e", "[0", QString("%1]").arg(lastErasedPage)};
  case Erase::All:
    return {"-e", "all"};
  default:
    return {};
  }
}

QString FlashPlan::summary() const
{
  switch (erase) {
  case Erase::None:
    return QObject::tr("Program only (STM32U375: every saved region is pinned and kept)");
  case Erase::BelowCalibration:
    return QObject::tr("Erase flash pages 0-%1 and program, keeping the calibration page")
        .arg(lastErasedPage);
  case Erase::All:
    if (noLayout)
      return QObject::tr("Mass erase and program (%1 image built before images recorded "
                         "their flash layout)")
          .arg(pageSize == 4096 ? "STM32U375" : "STM32L432");
    return QObject::tr("Mass erase and program (STM32L432 with no calibration page)");
  default:
    return error;
  }
}

FlashPlan planFlash(const QString &path)
{
  FlashPlan plan;
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    plan.error = QObject::tr("Cannot read %1").arg(path);
    return plan;
  }
  QString error;
  const auto symbols = elfSymbols(file.readAll(),
                                  {"__tag_flash_page_size__", "__calibration_start__",
                                   "__calibration_end__", "__flash0_base__",
                                   "__flash0_end__"},
                                  error);
  if (!error.isEmpty()) {
    plan.error = QObject::tr("%1 is %2; qtprogram needs the firmware's .elf, whose "
                             "symbols say what to erase.").arg(path, error);
    return plan;
  }
  if (!symbols.contains("__tag_flash_page_size__")) {
    // Built before the layout was recorded. Its calibration and persistent
    // regions cannot be trusted to be where a current tree puts them, so
    // erase everything; the flash size still says which MCU it is for.
    const quint32 size =
        symbols.value("__flash0_end__", 0) - symbols.value("__flash0_base__", 0);
    if (size == 256u * 1024u || size == 1024u * 1024u) {
      plan.noLayout = true;
      plan.pageSize = size == 256u * 1024u ? 2048 : 4096;
      plan.erase = FlashPlan::Erase::All;
    } else {
      plan.error = QObject::tr("%1 does not say which flash layout it was built for "
                               "(no __tag_flash_page_size__), nor for which MCU; "
                               "rebuild it from a current tree.").arg(path);
    }
    return plan;
  }
  plan.pageSize = static_cast<int>(symbols.value("__tag_flash_page_size__"));
  if (plan.pageSize == 4096) {
    plan.erase = FlashPlan::Erase::None;
  } else if (plan.pageSize == 2048) {
    const quint32 start = symbols.value("__calibration_start__", 0);
    const quint32 end = symbols.value("__calibration_end__", start);
    if (start > kFlashBase && end > start) {
      plan.erase = FlashPlan::Erase::BelowCalibration;
      plan.lastErasedPage = static_cast<int>((start - kFlashBase) / 2048) - 1;
    } else {
      plan.erase = FlashPlan::Erase::All;
    }
  } else {
    plan.error = QObject::tr("%1 was linked for %2-byte flash pages, which no tag in "
                             "this tree uses.").arg(path).arg(plan.pageSize);
  }
  return plan;
}
