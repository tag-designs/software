/**
 * @file    sha256.h
 * @brief   Minimal SHA-256 for hashing captured tag memory.
 *
 * @details Captures are keyed on the SHA-256 of what was read, and a firmware
 *          image is identified by the SHA-256 its build manifest records, so
 *          the host needs the hash without pulling in a crypto library. This
 *          is a straightforward FIPS 180-4 implementation; it is not
 *          constant-time and is not for security use.
 */

#ifndef TAGCORE_RECOVERY_SHA256_H
#define TAGCORE_RECOVERY_SHA256_H

#include <cstddef>
#include <cstdint>
#include <string>

namespace tagcore::recovery {

/**
 * @class   Sha256
 * @brief   Incremental SHA-256.
 *
 * @details Call Update() any number of times, then HexDigest() once. The
 *          object is not reusable after HexDigest().
 */
class Sha256 {
public:
  Sha256();

  /**
   * @brief   Absorb more input.
   * @param[in] data  Bytes to hash.
   * @param[in] len   Number of bytes.
   */
  void Update(const uint8_t *data, size_t len);

  /**
   * @brief   Finish and return the digest.
   * @return  64 lowercase hex characters.
   */
  std::string HexDigest();

  /**
   * @brief   Hash a whole buffer.
   * @param[in] data  Bytes to hash.
   * @param[in] len   Number of bytes.
   * @return  64 lowercase hex characters.
   */
  static std::string Of(const uint8_t *data, size_t len);

private:
  void Block(const uint8_t *block);

  uint32_t state_[8];   ///< Running hash state.
  uint8_t buffer_[64];  ///< Partial input block.
  size_t buffered_ = 0; ///< Bytes held in buffer_.
  uint64_t total_ = 0;  ///< Total input length in bytes.
};

} // namespace tagcore::recovery

#endif
