#pragma once

#include <libsesame3bt/ClientCore.h>

namespace esphome::sesame_lock {

// Preserve the feature layer's boolean command API without importing NimBLE.
class SesameProtocol : public libsesame3bt::core::SesameClientCore {
 public:
  using Core = libsesame3bt::core::SesameClientCore;
  using Status = libsesame3bt::core::Status;
  using History = libsesame3bt::core::History;
  static constexpr size_t MAX_CMD_TAG_SIZE = libsesame3bt::Sesame::MAX_HISTORY_TAG_SIZE;
  explicit SesameProtocol(libsesame3bt::core::SesameBLEBackend &backend) : Core(backend) {}

  bool lock(std::string_view tag) { return ok(Core::lock(tag)); }
  bool unlock(std::string_view tag) { return ok(Core::unlock(tag)); }
  bool lock(libsesame3bt::history_tag_type_t type,
            const std::array<std::byte, libsesame3bt::HISTORY_TAG_UUID_SIZE> &uuid) {
    return ok(Core::lock(type, uuid));
  }
  bool unlock(libsesame3bt::history_tag_type_t type,
              const std::array<std::byte, libsesame3bt::HISTORY_TAG_UUID_SIZE> &uuid) {
    return ok(Core::unlock(type, uuid));
  }
  bool click(std::string_view tag) { return ok(Core::click(tag)); }
  bool click(std::optional<uint8_t> script = std::nullopt) { return ok(Core::click(script)); }
  bool request_status() { return ok(Core::request_status()); }
  bool request_history() { return ok(Core::request_history()); }

 private:
  static bool ok(libsesame3bt::core::result_t result) {
    return result == libsesame3bt::core::result_t::success;
  }
};

}  // namespace esphome::sesame_lock
