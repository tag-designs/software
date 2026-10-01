/**
 * @file    targetimage.h
 * @brief   Minimal ELF32 reader for images the host downloads into a tag.
 *
 * @details Reads what SramCall needs from a little-endian ARM ELF32
 *          executable, such as an external-flash loader (`.stldr` is an ELF
 *          under another name): the loadable segments and the symbol table.
 *          No relocation, no debug information, and no external dependency.
 *
 * @see     host/libraries/tagcore/design/swd-recovery.md, "Library architecture"
 */

#ifndef TAGCORE_RECOVERY_TARGETIMAGE_H
#define TAGCORE_RECOVERY_TARGETIMAGE_H

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace tagcore::recovery {

/**
 * @struct  ImageSegment
 * @brief   One PT_LOAD segment.
 */
struct ImageSegment {
  uint32_t addr = 0;          ///< Load (physical) address.
  uint32_t memsz = 0;         ///< Size in memory; bytes past data.size() are zero.
  std::vector<uint8_t> data;  ///< File contents, p_filesz bytes.
};

/**
 * @class   TargetImage
 * @brief   The loadable segments and symbols of an ELF32 executable.
 *
 * @details Load() must succeed before any other call returns anything useful.
 *          Not thread-safe; the object is plain data once loaded.
 */
class TargetImage {
public:
  /**
   * @brief   Read an ELF32 little-endian ARM executable.
   *
   * @param[in]  path   File to read.
   * @param[out] error  Why it was rejected; may be nullptr.
   * @return  true when the header, program headers and symbol table parsed.
   *          An image with no symbol table is rejected, because every caller
   *          finds its entry points by name.
   */
  bool Load(const std::string &path, std::string *error = nullptr);

  /** @brief The PT_LOAD segments with a non-zero memory size, in file order. */
  const std::vector<ImageSegment> &Segments() const { return segments_; }

  /**
   * @brief   Look up a symbol's value.
   *
   * @param[in]  name   Symbol name.
   * @param[out] value  Its st_value. For a Thumb function this carries bit 0.
   * @return  false when no symbol has that name.
   */
  bool Symbol(const std::string &name, uint32_t &value) const;

  /**
   * @brief   Copy bytes at a load address out of the segments.
   *
   * @param[in]  addr  First byte.
   * @param[out] buf   Destination.
   * @param[in]  len   Bytes wanted.
   * @return  false unless one segment's file data covers the whole range.
   */
  bool ReadAt(uint32_t addr, void *buf, uint32_t len) const;

  /** @brief The file this image was loaded from. */
  const std::string &Path() const { return path_; }

private:
  std::string path_;
  std::vector<ImageSegment> segments_;
  std::map<std::string, uint32_t> symbols_;
};

} // namespace tagcore::recovery

#endif // TAGCORE_RECOVERY_TARGETIMAGE_H
