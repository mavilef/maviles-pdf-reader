// Testa o loop real da interface sem depender de um servidor X11.
#include "viewer.h"
#include "x11.h"
#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>

namespace {
using namespace pdf;
std::atomic<bool> release_zoom{false}, finished{false};
int stage = 0;
std::uint32_t center = 0, right_edge = 0;
constexpr std::uint32_t background = 0x1d2229;
auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
Window::Event key(unsigned k) {
    Window::Event event;
    event.kind = Window::Event::Key;
    event.key = k;
    return event;
}
}

namespace pdf {
// Documento e renderização controlados: o segundo render aguarda novas teclas.
Document::Document(const std::string &) { pages_.resize(3); }
Bitmap Renderer::render(std::size_t page, double zoom) {
    if (page == 0 && zoom > 1.6)
        while (!release_zoom)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
    return Bitmap(static_cast<int>(std::ceil(612 * zoom)),
                  static_cast<int>(std::ceil(792 * zoom)), 0x101010 * (page + 1));
}
Window::Socket::~Socket() = default;
Window::Window(int, int) {}
void Window::title(const std::string &) {}
void Window::sync() {}
void Window::present(const Bitmap &image) {
    center = image.pixels[400 * image.width + 500];
    right_edge = image.pixels[400 * image.width + 990];
}
Window::Event Window::next_event(int) {
    check(std::chrono::steady_clock::now() < deadline, "Timeout no loop de interface");
    if (stage == 0 && center == 0x101010) {
        check(right_edge == background, "Página inicial fora da largura esperada");
        ++stage;
        return key('+');
    }
    if (stage == 1) {
        check(right_edge == 0x101010, "Zoom não mostrou prévia imediata");
        ++stage;
        return key('n');
    }
    if (stage == 2) {
        check(center == background, "Exibiu imagem da página anterior na nova página");
        ++stage;
        // Salta outra página antes de o zoom anterior concluir.
        release_zoom = true;
        return key('n');
    }
    if (stage == 3 && center == 0x303030) {
        ++stage;
        return key('p');
    }
    if (stage == 4 && center == 0x202020) {
        ++stage;
        return key(0xff55); // Page Up
    }
    if (stage == 5 && center == 0x101010) {
        ++stage;
        return key(0xff56); // Page Down: reutiliza a imagem em cache.
    }
    if (stage == 6) {
        check(center == 0x202020, "Troca de página não reutilizou o cache");
        ++stage;
        return key('q');
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    return {};
}
}

int main() {
    // Detecta regressão para renderização bloqueante, mesmo antes de next_event.
    std::thread watchdog([] {
        while (!finished && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        if (!finished) {
            std::cerr << "Interface bloqueada durante renderização\n";
            std::abort();
        }
    });
    int result = 0;
    try {
        pdf::Document document("fixture");
        pdf::show_document(document, 0);
        pdf::check(stage == 7, "Sequência de navegação incompleta");
        std::cout << "OK: prévia de zoom, comandos durante renderização, N/P, PgUp/PgDn e cache\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        result = 1;
    }
    finished = true;
    watchdog.join();
    return result;
}
