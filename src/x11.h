#pragma once
#include "common.h"
#include <deque>

namespace pdf {
// Cliente mínimo do protocolo X11 por socket UNIX; não usa libX11/libxcb.
class Window {
    struct Socket {
        int fd = -1;
        ~Socket();
    } socket_;
    std::uint32_t window_ = 0, gc_ = 0, protocols_ = 0, close_atom_ = 0;
    unsigned depth_ = 0, max_request_ = 0, min_key_ = 0, keys_per_code_ = 0;
    std::vector<std::uint32_t> keys_;
    std::deque<Bytes> events_;
    void send(const Bytes &bytes);
    Bytes receive(std::size_t size);
    void request(Bytes bytes);
    Bytes packet();
    Bytes reply();
    std::uint32_t atom(const std::string &name);
    void property(std::uint32_t atom, std::uint32_t type, unsigned format, const Bytes &bytes);

  public:
    struct Event {
        enum Kind { Other, Expose, Resize, Key, Button, Close } kind = Other;
        int width = 0, height = 0, x = 0, y = 0;
        unsigned key = 0, modifiers = 0, button = 0;
    };
    Window(int width, int height);
    void title(const std::string &name);
    void present(const Bitmap &image);
    Event next_event(int timeout_ms = -1);
    void sync();
};
} // namespace pdf
