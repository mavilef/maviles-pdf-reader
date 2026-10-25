#include "x11.h"
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace pdf {
namespace {
void put16(Bytes &b, std::size_t p, unsigned v) {
    b.at(p) = v & 255;
    b.at(p + 1) = (v >> 8) & 255;
}
void put32(Bytes &b, std::size_t p, std::uint32_t v) {
    put16(b, p, v & 65535);
    put16(b, p + 2, v >> 16);
}
unsigned get16(const Bytes &b, std::size_t p) {
    return b.at(p) + 256u * b.at(p + 1);
}
std::uint32_t get32(const Bytes &b, std::size_t p) {
    return get16(b, p) + 65536u * get16(b, p + 2);
}
std::size_t padded(std::size_t n) {
    return (n + 3) & ~std::size_t(3);
}
Bytes cookie(const std::string &display) {
    const char *auth = std::getenv("XAUTHORITY");
    const char *home = std::getenv("HOME");
    const std::string path = auth ? auth : home ? std::string(home) + "/.Xauthority" : "";
    std::ifstream f(path, std::ios::binary);
    if (!f)
        return {};
    Bytes bytes((std::istreambuf_iterator<char>(f)), {});
    std::size_t p = 0;
    auto word = [&]() {
        check(p + 2 <= bytes.size(), "Xauthority truncado");
        unsigned v = bytes[p] * 256u + bytes[p + 1];
        p += 2;
        return v;
    };
    auto field = [&]() {
        const unsigned n = word();
        check(n <= bytes.size() - p, "Xauthority inválido");
        std::string s(reinterpret_cast<const char *>(bytes.data() + p), n);
        p += n;
        return s;
    };
    while (p < bytes.size()) {
        const unsigned family = word();
        const auto address = field(), number = field(), name = field(), value = field();
        (void)address;
        if ((family == 256 || family == 65535) && number == display && name == "MIT-MAGIC-COOKIE-1")
            return Bytes(value.begin(), value.end());
    }
    return {};
}
} // namespace
Window::Socket::~Socket() {
    if (fd >= 0)
        ::close(fd);
}
void Window::send(const Bytes &bytes) {
    std::size_t sent = 0;
    while (sent < bytes.size()) {
        const auto n = ::send(socket_.fd, bytes.data() + sent, bytes.size() - sent, MSG_NOSIGNAL);
        if (n < 0 && errno == EINTR)
            continue;
        check(n > 0, "Conexão X11 encerrada ao enviar dados");
        sent += static_cast<std::size_t>(n);
    }
}
Bytes Window::receive(std::size_t size) {
    check(size <= 64 * 1024 * 1024, "Resposta X11 grande demais");
    Bytes bytes(size);
    std::size_t received = 0;
    while (received < size) {
        const auto n = ::recv(socket_.fd, bytes.data() + received, size - received, 0);
        if (n < 0 && errno == EINTR)
            continue;
        check(n > 0, "Conexão X11 encerrada");
        received += static_cast<std::size_t>(n);
    }
    return bytes;
}
void Window::request(Bytes bytes) {
    bytes.resize(padded(bytes.size()));
    check(bytes.size() / 4 <= max_request_, "Requisição X11 excedida");
    put16(bytes, 2, bytes.size() / 4);
    send(bytes);
}
Bytes Window::packet() {
    auto bytes = receive(32);
    const unsigned type = bytes[0] & 127;
    if (type == 0)
        throw std::runtime_error("Erro X11 " + std::to_string(bytes[1]) + " no opcode " +
                                 std::to_string(bytes[10]));
    if (type == 1 || type == 35) {
        const auto extra = receive(static_cast<std::size_t>(get32(bytes, 4)) * 4);
        bytes.insert(bytes.end(), extra.begin(), extra.end());
    }
    return bytes;
}
Bytes Window::reply() {
    for (;;) {
        auto p = packet();
        if (p[0] == 1)
            return p;
        check(events_.size() < 10000, "Fila de eventos X11 excedida");
        events_.push_back(std::move(p));
    }
}
std::uint32_t Window::atom(const std::string &name) {
    Bytes r(8 + name.size());
    r[0] = 16;
    put16(r, 4, name.size());
    std::copy(name.begin(), name.end(), r.begin() + 8);
    request(std::move(r));
    return get32(reply(), 8);
}
void Window::property(std::uint32_t property_atom, std::uint32_t type, unsigned format,
                      const Bytes &data) {
    Bytes r(24 + data.size());
    r[0] = 18;
    put32(r, 4, window_);
    put32(r, 8, property_atom);
    put32(r, 12, type);
    r[16] = format;
    put32(r, 20, data.size() / (format / 8));
    std::copy(data.begin(), data.end(), r.begin() + 24);
    request(std::move(r));
}
Window::Window(int width, int height) {
    const char *env = std::getenv("DISPLAY");
    check(env, "Sem DISPLAY. Use uma sessão X11/XWayland ou --render para exportar uma página.");
    std::string display = env;
    if (display.rfind("unix:", 0) == 0)
        display.erase(0, 4);
    check(!display.empty() && display[0] == ':',
          "Esta versão aceita somente DISPLAY local (:0, :1...)");
    const auto dot = display.find('.');
    const std::string number = display.substr(1, dot == std::string::npos ? dot : dot - 1);
    check(!number.empty() && number.find_first_not_of("0123456789") == std::string::npos,
          "DISPLAY inválido");
    check(dot == std::string::npos || display.substr(dot) == ".0",
          "Apenas a tela X11 0 é suportada");
    socket_.fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    check(socket_.fd >= 0, "Não foi possível criar socket X11");
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    const auto path = "/tmp/.X11-unix/X" + number;
    check(path.size() < sizeof(address.sun_path), "DISPLAY longo demais");
    std::memcpy(address.sun_path, path.c_str(), path.size() + 1);
    check(::connect(socket_.fd, reinterpret_cast<sockaddr *>(&address), sizeof(address)) == 0,
          "Não foi possível conectar ao servidor X11 local");
    const auto auth = cookie(number);
    const std::string auth_name = auth.empty() ? "" : "MIT-MAGIC-COOKIE-1";
    Bytes hello(12 + padded(auth_name.size()) + padded(auth.size()));
    hello[0] = 'l';
    put16(hello, 2, 11);
    put16(hello, 6, auth_name.size());
    put16(hello, 8, auth.size());
    std::copy(auth_name.begin(), auth_name.end(), hello.begin() + 12);
    std::copy(auth.begin(), auth.end(), hello.begin() + 12 + padded(auth_name.size()));
    send(hello);
    const auto prefix = receive(8), setup = receive(get16(prefix, 6) * 4);
    check(prefix[0] == 1, "Servidor X11 recusou a conexão; confira DISPLAY/XAUTHORITY");
    check(setup.size() >= 32 && setup[20] > 0, "Setup X11 inválido");
    const auto base = get32(setup, 4), mask = get32(setup, 8);
    check((mask & 3) == 3, "Faixa de IDs X11 não suportada");
    window_ = base | 1;
    gc_ = base | 2;
    max_request_ = get16(setup, 18);
    min_key_ = setup[26];
    const unsigned max_key = setup[27];
    check(setup[22] == 0, "Servidor X11 com imagens big-endian não suportado");
    std::size_t p = 32 + padded(get16(setup, 16));
    bool pixel_format = false;
    for (unsigned i = 0; i < setup[21]; ++i, p += 8)
        if (setup.at(p) == 24 && setup.at(p + 1) == 32 && setup.at(p + 2) == 32)
            pixel_format = true;
    check(pixel_format && p + 40 <= setup.size(),
          "X11 precisa de visual RGB de 24 bits / pixel de 32 bits");
    const auto root = get32(setup, p), visual = get32(setup, p + 32);
    depth_ = setup[p + 38];
    check(depth_ == 24, "Profundidade X11 diferente de 24 bits não suportada");
    bool visual_ok = false;
    const unsigned depths = setup[p + 39];
    p += 40;
    for (unsigned i = 0; i < depths; ++i) {
        const unsigned visuals = get16(setup, p + 2);
        p += 8;
        for (unsigned j = 0; j < visuals; ++j, p += 24)
            if (get32(setup, p) == visual)
                visual_ok = setup.at(p + 4) == 4 && get32(setup, p + 8) == 0xff0000 &&
                            get32(setup, p + 12) == 0xff00 && get32(setup, p + 16) == 0xff;
    }
    check(visual_ok, "Visual X11 não é TrueColor RGB padrão");
    Bytes create(40);
    create[0] = 1;
    create[1] = depth_;
    put32(create, 4, window_);
    put32(create, 8, root);
    put16(create, 16, width);
    put16(create, 18, height);
    put16(create, 22, 1);
    put32(create, 24, visual);
    put32(create, 28, (1u << 1) | (1u << 11));
    put32(create, 32, 0x20252b);
    put32(create, 36, (1u << 0) | (1u << 2) | (1u << 15) | (1u << 17));
    request(std::move(create));
    Bytes gc(16);
    gc[0] = 55;
    put32(gc, 4, gc_);
    put32(gc, 8, window_);
    request(std::move(gc));
    protocols_ = atom("WM_PROTOCOLS");
    close_atom_ = atom("WM_DELETE_WINDOW");
    Bytes close(4);
    put32(close, 0, close_atom_);
    property(protocols_, 4, 32, close);
    Bytes keyboard(8);
    keyboard[0] = 101;
    keyboard[4] = min_key_;
    keyboard[5] = max_key - min_key_ + 1;
    request(std::move(keyboard));
    const auto mapping = reply();
    keys_per_code_ = mapping[1];
    check(keys_per_code_ > 0, "Mapa de teclado X11 vazio");
    for (std::size_t k = 32; k + 4 <= mapping.size(); k += 4)
        keys_.push_back(get32(mapping, k));
    title("Maviles PDF Reader - C++ do zero");
    Bytes map(8);
    map[0] = 8;
    put32(map, 4, window_);
    request(std::move(map));
}
void Window::title(const std::string &name) {
    property(39, 31, 8, Bytes(name.begin(), name.end()));
}
void Window::present(const Bitmap &image) {
    check(image.width <= 16000 && image.height <= 16000, "Janela grande demais");
    const int rows = std::max(1, static_cast<int>((max_request_ * 4 - 24) / (image.width * 4)));
    for (int y = 0; y < image.height; y += rows) {
        const int n = std::min(rows, image.height - y);
        Bytes r(24 + static_cast<std::size_t>(image.width) * n * 4);
        r[0] = 72;
        r[1] = 2;
        put32(r, 4, window_);
        put32(r, 8, gc_);
        put16(r, 12, image.width);
        put16(r, 14, n);
        put16(r, 18, y);
        r[21] = depth_;
        for (int i = 0; i < image.width * n; ++i)
            put32(r, 24 + static_cast<std::size_t>(i) * 4, image.pixels[y * image.width + i]);
        request(std::move(r));
    }
}
Window::Event Window::next_event(int timeout_ms) {
    if (events_.empty() && timeout_ms >= 0) {
        pollfd descriptor{socket_.fd, POLLIN, 0};
        int ready;
        do {
            ready = ::poll(&descriptor, 1, timeout_ms);
        } while (ready < 0 && errno == EINTR);
        check(ready >= 0, "Falha ao aguardar eventos X11");
        if (!ready)
            return {};
    }
    Bytes raw;
    if (events_.empty())
        raw = packet();
    else {
        raw = std::move(events_.front());
        events_.pop_front();
    }
    Event event;
    const unsigned type = raw[0] & 127;
    if (type == 12)
        event.kind = Event::Expose;
    else if (type == 22) {
        event.kind = Event::Resize;
        event.width = get16(raw, 20);
        event.height = get16(raw, 22);
    } else if (type == 2) {
        event.kind = Event::Key;
        event.modifiers = get16(raw, 28);
        const int code = raw[1] - static_cast<int>(min_key_);
        const unsigned shift = (event.modifiers & 1) ? 1 : 0;
        if (code >= 0 && static_cast<std::size_t>(code) * keys_per_code_ + shift < keys_.size()) {
            event.key = keys_[code * keys_per_code_ + shift];
            if (!event.key)
                event.key = keys_[code * keys_per_code_];
        }
    } else if (type == 4) {
        event.kind = Event::Button;
        event.button = raw[1];
        event.x = static_cast<std::int16_t>(get16(raw, 24));
        event.y = static_cast<std::int16_t>(get16(raw, 26));
        event.modifiers = get16(raw, 28);
    } else if (type == 17 || (type == 33 && raw[1] == 32 && get32(raw, 8) == protocols_ &&
                              get32(raw, 12) == close_atom_))
        event.kind = Event::Close;
    return event;
}
void Window::sync() {
    Bytes request_data(4);
    request_data[0] = 43;
    request(std::move(request_data));
    (void)reply();
}
} // namespace pdf
