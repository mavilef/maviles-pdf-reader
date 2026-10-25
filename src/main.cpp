#include "viewer.h"
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <iostream>

namespace {
void usage() {
    std::cout << "Maviles PDF Reader em C++17, sem bibliotecas externas\n\n"
                 "  maviles-pdf-reader [arquivo.pdf] [--page N]\n"
                 "  maviles-pdf-reader arquivo.pdf --info\n"
                 "  maviles-pdf-reader arquivo.pdf --render N saida.ppm [--scale 1.0]\n"
                 "  maviles-pdf-reader arquivo.pdf --check [--scale 1.0]\n"
                 "  maviles-pdf-reader arquivo.pdf --smoke\n\n"
                 "Após make install, mpr é um atalho para maviles-pdf-reader.\n"
                 "Sem arquivo, abre o único PDF em ~/Downloads.\n"
                 "Janela: X11/XWayland local, RGB 24 bits.\n"
                 "Teclas: N/P ou PgDn/PgUp páginas, HJKL mover, +/- zoom, F largura, número+Enter, Q sair.\n";
}
long long positive(const std::string &text) {
    std::size_t used = 0;
    const auto n = std::stoll(text, &used);
    pdf::check(used == text.size() && n > 0, "Número de página inválido");
    return n;
}
std::string default_pdf() {
    const char *home = std::getenv("HOME");
    pdf::check(home, "HOME não definido; informe o arquivo PDF");
    const auto folder = std::filesystem::path(home) / "Downloads";
    std::vector<std::string> files;
    if (std::filesystem::is_directory(folder))
        for (const auto &entry : std::filesystem::directory_iterator(folder)) {
            auto ext = entry.path().extension().string();
            for (auto &c : ext)
                if (c >= 'A' && c <= 'Z')
                    c += 32;
            if (entry.is_regular_file() && ext == ".pdf")
                files.push_back(entry.path().string());
        }
    pdf::check(files.size() == 1, "Informe o caminho do PDF: ~/Downloads precisa conter exatamente "
                                  "um PDF para abertura automática");
    return files[0];
}
} // namespace
int main(int argc, char **argv) {
    try {
        std::string file, output;
        std::size_t page = 0;
        double scale = 1.0;
        enum Mode { View, Info, Render, Check, Smoke };
        Mode mode = View;
        bool chosen = false;
        auto choose = [&](Mode next) {
            pdf::check(!chosen, "Escolha apenas um modo de execução");
            chosen = true;
            mode = next;
        };
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            auto next = [&]() {
                pdf::check(i + 1 < argc, "Falta argumento para " + arg);
                return std::string(argv[++i]);
            };
            if (arg == "--help" || arg == "-h") {
                usage();
                return 0;
            }
            if (arg == "--info")
                choose(Info);
            else if (arg == "--check")
                choose(Check);
            else if (arg == "--smoke")
                choose(Smoke);
            else if (arg == "--page")
                page = static_cast<std::size_t>(positive(next()) - 1);
            else if (arg == "--render") {
                choose(Render);
                page = static_cast<std::size_t>(positive(next()) - 1);
                output = next();
            } else if (arg == "--scale") {
                const auto value = next();
                std::size_t used = 0;
                scale = std::stod(value, &used);
                pdf::check(used == value.size() && std::isfinite(scale) && scale > 0 && scale <= 4,
                           "Escala inválida");
            } else {
                pdf::check(arg.rfind("--", 0) != 0 && file.empty(), "Argumento inválido: " + arg);
                file = arg;
            }
        }
        // O fechamento do terminal não deve encerrar a janela do leitor.
        // Modos de linha de comando mantêm o tratamento normal do sinal.
        if (mode == View)
            pdf::check(std::signal(SIGHUP, SIG_IGN) != SIG_ERR,
                       "Não foi possível desvincular o fechamento da janela do terminal");
        if (file.empty())
            file = default_pdf();
        pdf::Document document(file);
        pdf::check(page < document.pages().size(), "Página fora do documento");
        if (mode == Info) {
            std::cout << "Arquivo: " << file << "\nPáginas: " << document.pages().size()
                      << "\nPrimeira página: " << document.pages()[0].width << " x "
                      << document.pages()[0].height << " pontos\n";
        } else if (mode == Render) {
            pdf::check(std::filesystem::weakly_canonical(output) !=
                           std::filesystem::weakly_canonical(file),
                       "A saída não pode sobrescrever o PDF");
            pdf::Renderer renderer(document);
            renderer.render(page, scale).save_ppm(output);
            std::cout << "Página " << page + 1 << " renderizada em " << output << '\n';
        } else if (mode == Check) {
            pdf::Renderer renderer(document);
            std::size_t errors = 0;
            for (std::size_t i = 0; i < document.pages().size(); ++i) {
                try {
                    (void)renderer.render(i, scale);
                } catch (const std::exception &e) {
                    ++errors;
                    std::cerr << "Página " << i + 1 << ": " << e.what() << '\n';
                }
                if ((i + 1) % 50 == 0)
                    std::cout << i + 1 << " / " << document.pages().size() << std::endl;
            }
            std::cout << document.pages().size() << " páginas verificadas; " << errors
                      << " erros\n";
            return errors ? 1 : 0;
        } else
            pdf::show_document(document, page, mode == Smoke);
    } catch (const std::exception &e) {
        std::cerr << "Erro: " << e.what() << '\n';
        return 1;
    }
}
