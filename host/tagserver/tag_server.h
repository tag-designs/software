/**
 * @file    tag_server.h
 * @brief   The TagService handlers: the tagcore Tag API over gRPC.
 *
 * @details TagServer implements every call of tagrpc.TagService
 *          (proto/tag_service.proto) as a thin wrapper around one Tag
 *          method, and turns failures into gRPC status codes with a
 *          google.rpc.Status and a tagrpc.TagError in the details. grpcxx
 *          dispatches each call to the matching call<Rpc>() specialisation;
 *          a call without one fails to link, which keeps the mirror complete.
 *
 * @see     host/libraries/tagcore/design/proposals/tag-server.md
 */
#ifndef TAG_SERVER_H
#define TAG_SERVER_H

#include <grpcxx/context.h>

#include "tag_service.grpcxx.pb.h"

class Tag;

/**
 * @class   TagServer
 * @brief   Serves one Tag through tagrpc.TagService.
 *
 * @details Holds no state of its own beyond the Tag reference and the debug
 *          switch. Tag serialises its own calls, so grpcxx may run handlers
 *          on several worker threads at once. The Tag must outlive the
 *          server.
 */
class TagServer
{
public:
  /**
   * @param[in] tag            The tag to serve; attached or not.
   * @param[in] debug_enabled  Serve the memory side channel and the
   *                           field-rescue calls. Otherwise they return
   *                           PERMISSION_DENIED.
   */
  TagServer(Tag &tag, bool debug_enabled) : tag_(tag), debug_enabled_(debug_enabled) {}

  /// Handles one call; specialised for every rpc in tagrpc::TagService.
  template <typename Rpc>
  typename Rpc::result_type call(grpcxx::context &ctx, const typename Rpc::request_type &request);

  Tag &tag() { return tag_; }
  bool debug_enabled() const { return debug_enabled_; }

private:
  Tag &tag_;
  bool debug_enabled_;
};

#endif
