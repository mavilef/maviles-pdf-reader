#include "viewer.h"
#include "x11.h"
#include <iostream>
#include <future>
#include <chrono>
#include <deque>

namespace pdf {
void show_document(Document &document, std::size_t initial_page, bool smoke_test) {
    int width = 1000, height = 850, scroll_y = 0, scroll_x = 0;
    std::size_t page = initial_page;
    double zoom = 1;
    bool fit = true, needs_render = true;
    std::string input;
    Renderer renderer(document);
    struct CachedPage {
        std::size_t page;
        double zoom;
        Bitmap bitmap;
    };
    // Apenas o worker acessa o Renderer; a interface usa bitmaps já concluídos.
    std::deque<CachedPage> cache;
    std::future<Bitmap> pending;
    std::size_t rendering_page = 0;
    double rendering_zoom = 1;
    auto collect = [&]() {
        try {
            auto bitmap = pending.get();
            cache.push_front({rendering_page, rendering_zoom, std::move(bitmap)});
            std::size_t pixels = 0;
            for (const auto &entry : cache)
                pixels += entry.bitmap.pixels.size();
            while (cache.size() > 1 && (cache.size() > 4 || pixels > 16 * 1024 * 1024)) {
                pixels -= cache.back().bitmap.pixels.size();
                cache.pop_back();
            }
        } catch (const std::exception &e) {
            std::cerr << "Página " << rendering_page + 1 << ": " << e.what() << '\n';
        }
    };
    Window window(width, height);
    window.title("Maviles PDF Reader");
    auto go = [&](long long target) {
        if (target < 0 || static_cast<std::size_t>(target) >= document.pages().size())
            return;
        page = static_cast<std::size_t>(target);
        scroll_y = scroll_x = 0;
        needs_render = true;
        input.clear();
    };
    auto change_zoom = [&](double factor) {
        fit = false;
        zoom = std::clamp(zoom * factor, 0.1, 3.0);
        needs_render = true;
    };
    auto draw = [&]() {
        if (fit)
            zoom = std::clamp((width - 48) / document.pages()[page].width, 0.1, 3.0);
        const CachedPage *preview = nullptr;
        for (const auto &entry : cache) {
            if (entry.page != page)
                continue;
            if (!preview || std::abs(entry.zoom - zoom) < std::abs(preview->zoom - zoom))
                preview = &entry;
        }
        if (preview && preview->zoom == zoom)
            needs_render = false;
        if (needs_render && !pending.valid()) {
            rendering_page = page;
            rendering_zoom = zoom;
            pending = std::async(std::launch::async, [&, page, zoom] {
                return renderer.render(page, zoom);
            });
            needs_render = false;
        }
        const int page_width = static_cast<int>(std::ceil(document.pages()[page].width * zoom));
        const int page_height = static_cast<int>(std::ceil(document.pages()[page].height * zoom));
        scroll_y = std::clamp(scroll_y, 0, std::max(0, page_height - height + 48));
        scroll_x = std::clamp(scroll_x, 0, std::max(0, page_width - width + 48));
        Bitmap screen(width, height, 0x1d2229);
        const int left = std::max(24, (width - page_width) / 2) - scroll_x;
        for (int y = 0; y < height; ++y) {
            const int sy = y - 24 + scroll_y;
            if (sy < 0 || sy >= page_height)
                continue;
            for (int x = 0; x < width; ++x) {
                const int sx = x - left;
                if (sx >= 0 && sx < page_width && preview) {
                    const auto &bitmap = preview->bitmap;
                    const int px = std::min(bitmap.width - 1,
                        static_cast<int>(sx * preview->zoom / zoom));
                    const int py = std::min(bitmap.height - 1,
                        static_cast<int>(sy * preview->zoom / zoom));
                    screen.pixels[y * width + x] = bitmap.pixels[py * bitmap.width + px];
                }
            }
        }
        window.present(screen);
    };
    draw();
    if (smoke_test) {
        if (pending.valid()) {
            collect();
            draw();
        }
        window.sync();
        std::cout << "OK: janela X11 criada e imagem enviada\n";
        return;
    }
    for (;;) {
        if (pending.valid() && pending.wait_for(std::chrono::milliseconds(0)) ==
                                   std::future_status::ready) {
            collect();
            draw();
        }
        const auto e = window.next_event(pending.valid() ? 16 : -1);
        bool dirty = true;
        if (e.kind == Window::Event::Close)
            break;
        if (e.kind == Window::Event::Resize) {
            if (width == e.width && height == e.height)
                continue;
            check(e.width > 0 && e.height > 0 && e.width <= 8000 && e.height <= 8000,
                  "Tamanho de janela não suportado");
            width = e.width;
            height = e.height;
            if (fit)
                needs_render = true;
        } else if (e.kind == Window::Event::Key) {
            const unsigned key = e.key;
            if (key == 'q' || key == 'Q')
                break;
            if (key >= '0' && key <= '9') {
                if (input.size() < 6)
                    input += static_cast<char>(key);
            } else if (key == 0xff0d) {
                if (!input.empty()) {
                    const auto target = std::stoll(input);
                    input.clear();
                    go(target - 1);
                }
            } else if (key == 0xff08) {
                if (!input.empty())
                    input.pop_back();
            } else if (key == 0xff1b)
                input.clear();
            else if (key == 0xff56 || key == 0xff53 || key == ' ' || key == 'n' || key == 0xff9b)
                go(static_cast<long long>(page) + 1);
            else if (key == 0xff55 || key == 0xff51 || key == 'p' || key == 0xff9a)
                go(static_cast<long long>(page) - 1);
            else if (key == 0xff50)
                go(0);
            else if (key == 0xff57)
                go(document.pages().size() - 1);
            else if (key == '+' || key == '=' || key == 0xffab)
                change_zoom(1.25);
            else if (key == '-' || key == 0xffad)
                change_zoom(0.8);
            else if (key == 'f' || key == 'F') {
                fit = true;
                needs_render = true;
            } else if (key == 'h')
                scroll_x -= 60;
            else if (key == 'l')
                scroll_x += 60;
            else if (key == 0xff52 || key == 'k')
                scroll_y -= 60;
            else if (key == 0xff54 || key == 'j')
                scroll_y += 60;
            else
                dirty = false;
        } else if (e.kind == Window::Event::Button) {
            if (e.button == 4 || e.button == 5) {
                const int direction = e.button == 4 ? -1 : 1;
                if (e.modifiers & 4)
                    change_zoom(direction < 0 ? 1.25 : 0.8);
                else if (e.modifiers & 1)
                    scroll_x += direction * 80;
                else
                    scroll_y += direction * 80;
            } else
                dirty = false;
        } else if (e.kind != Window::Event::Expose)
            dirty = false;
        if (dirty)
            draw();
    }
}
} // namespace pdf
