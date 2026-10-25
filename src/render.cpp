#include "render.h"
#include "pdf_image.h"
#include "raster.h"
#include <functional>

namespace pdf {
Renderer::Font &Renderer::font(const Value &ref) {
    check(ref.kind == Value::Reference, "Fonte deve ser objeto indireto");
    auto found = fonts_.find(ref.id);
    if (found != fonts_.end())
        return found->second;
    const auto &v = document_.resolve(ref);
    if (v["Subtype"].is("Type1")) {
        const auto &file = document_.resolve(v["FontDescriptor"])["FontFile3"];
        check(file.kind == Value::Reference && document_.resolve(file)["Subtype"].is("Type1C"),
              "Fonte Type1 sem CFF embutido");
        auto &face = cff_faces_[file.id];
        if (!face)
            face = std::make_shared<CFF>(document_.stream(file));
        Font f;
        f.cff = face;
        const auto &encoding = document_.resolve(v["Encoding"]);
        const auto &base = encoding.kind == Value::Dictionary ? encoding["BaseEncoding"] : encoding;
        check(base.kind == Value::Null || base.is("WinAnsiEncoding") || base.is("StandardEncoding"),
              "Encoding Type1 não suportado");
        for (int c = 0; c < 256; ++c)
            f.codes[c] = base.kind == Value::Null
                             ? face->encoded(c)
                             : face->named(CFF::standard_name(c, base.is("WinAnsiEncoding")));
        int code = 0;
        for (const auto &v : document_.resolve(encoding["Differences"]).array) {
            if (v.kind == Value::Number)
                code = v.integer();
            else {
                check(code >= 0 && code < 256 && v.kind == Value::Name, "Differences inválido");
                f.codes[code++] = face->named(v.text);
            }
        }
        const int first = v["FirstChar"].integer();
        const auto &widths = document_.resolve(v["Widths"]).array;
        check(first >= 0 && first <= 255 && widths.size() <= std::size_t(256 - first),
              "Widths Type1 inválido");
        for (std::size_t i = 0; i < widths.size(); ++i)
            f.widths[first + int(i)] = widths[i].number;
        return fonts_.emplace(ref.id, std::move(f)).first->second;
    }
    check(v["Subtype"].is("Type0") && v["Encoding"].is("Identity-H"),
          "Fonte não suportada (esperado Type0 / Identity-H)");
    const auto &descendants = document_.resolve(v["DescendantFonts"]);
    check(descendants.array.size() == 1, "Fonte sem DescendantFonts");
    const auto &cid = document_.resolve(descendants.array[0]);
    check(cid["Subtype"].is("CIDFontType2"), "Fonte CID não TrueType");
    const auto &file = document_.resolve(cid["FontDescriptor"])["FontFile2"];
    check(file.kind == Value::Reference, "Fonte TrueType não embutida");
    auto &face = faces_[file.id];
    if (!face)
        face = std::make_shared<TrueType>(document_.stream(file));
    Font f;
    f.face = face;
    if (cid["DW"].kind == Value::Number)
        f.default_width = cid["DW"].number;
    const auto &widths = document_.resolve(cid["W"]).array;
    for (std::size_t i = 0; i < widths.size();) {
        const int start = widths[i++].integer();
        check(i < widths.size() && start >= 0 && start <= 65535, "Larguras CID inválidas");
        const auto &next = document_.resolve(widths[i++]);
        if (next.kind == Value::Array) {
            check(next.array.size() <= static_cast<std::size_t>(65536 - start),
                  "Larguras CID excedidas");
            for (std::size_t j = 0; j < next.array.size(); ++j)
                f.widths[start + static_cast<int>(j)] = next.array[j].number;
        } else {
            const int end = next.integer();
            check(i < widths.size() && end >= start && end <= 65535, "Intervalo CID inválido");
            const double width = widths[i++].number;
            for (int j = start; j <= end; ++j)
                f.widths[j] = width;
        }
    }
    if (cid["CIDToGIDMap"].kind == Value::Reference)
        f.cid_map = document_.stream(cid["CIDToGIDMap"]);
    else
        check(cid["CIDToGIDMap"].kind == Value::Null || cid["CIDToGIDMap"].is("Identity"),
              "CIDToGIDMap não suportado");
    return fonts_.emplace(ref.id, std::move(f)).first->second;
}
Bitmap Renderer::render(std::size_t index, double scale) {
    check(index < document_.pages().size(), "Página fora do documento");
    check(std::isfinite(scale) && scale > 0 && scale <= 4, "Zoom inválido (máximo 400%)");
    const auto &page = document_.pages()[index];
    const double dw = std::ceil(page.width * scale), dh = std::ceil(page.height * scale);
    check(dw <= 16000 && dh <= 16000 && dw * dh <= 16 * 1024 * 1024,
          "Página excede o limite de renderização; diminua o zoom");
    const int width = static_cast<int>(dw), height = static_cast<int>(dh);
    Raster raster(width * 2, height * 2);
    struct State {
        Matrix ctm, text, line;
        Clip clip;
        std::uint32_t fill = 0, stroke = 0;
        double line_width = 1, size = 12, spacing = 0, word_spacing = 0, hscale = 1, leading = 0,
               rise = 0;
        std::string font;
        int text_mode = 0, cap = 0, join = 0;
        double miter = 4, dash_phase = 0;
        std::vector<double> dash;
    } s;
    s.ctm = {scale * 2, 0, 0, -scale * 2, -page.x * scale * 2, (page.y + page.height) * scale * 2};
    s.clip = {0, 0, double(width * 2), double(height * 2)};
    std::vector<State> saved;
    Path path;
    bool clipping = false, clip_even_odd = false;
    auto number = [](const Value &v) {
        check(v.kind == Value::Number, "Operando numérico esperado");
        return v.number;
    };
    auto color = [](double r, double g, double b) {
        auto channel = [](double x) {
            return static_cast<unsigned>(std::clamp(x, 0.0, 1.0) * 255 + 0.5);
        };
        return (channel(r) << 16) | (channel(g) << 8) | channel(b);
    };
    auto end_path = [&]() {
        if (clipping) {
            s.clip = raster.intersect(path, s.clip, clip_even_odd);
        }
        clipping = false;
        path.clear();
    };
    auto stroke = [&] {
        const double scale = std::hypot(s.ctm.a, s.ctm.b);
        auto dash = s.dash;
        for (auto &n : dash)
            n *= scale;
        if (scale > 0)
            raster.stroke(path, s.line_width * scale, s.stroke, s.clip, s.cap, s.join, s.miter,
                          dash, s.dash_phase * scale);
    };
    auto text = [&](const Value &value, const Value &fonts) {
        auto &f = font(fonts[s.font]);
        check(value.kind == Value::String && (f.cff || value.text.size() % 2 == 0),
              "Texto PDF inválido");
        check(s.text_mode == 0 || s.text_mode == 3, "Modo de texto ainda não suportado");
        for (std::size_t i = 0; i < value.text.size(); i += f.cff ? 1 : 2) {
            const int cid = f.cff ? static_cast<unsigned char>(value.text[i])
                                  : static_cast<unsigned char>(value.text[i]) * 256 +
                                        static_cast<unsigned char>(value.text[i + 1]);
            int gid = f.cff ? f.codes[cid] : cid;
            if (!f.cid_map.empty()) {
                check(static_cast<std::size_t>(cid * 2 + 1) < f.cid_map.size(),
                      "CID fora do mapa de glifos");
                gid = f.cid_map[cid * 2] * 256 + f.cid_map[cid * 2 + 1];
            }
            if (s.text_mode == 0) {
                const double unit = s.size / (f.cff ? 1000 : f.face->units());
                const Matrix transform =
                    s.ctm * s.text * Matrix{unit * s.hscale, 0, 0, unit, 0, s.rise};
                Path glyph = f.cff ? f.cff->glyph(gid) : f.face->glyph(gid);
                for (auto &contour : glyph)
                    for (auto &p : contour)
                        p = transform(p);
                raster.fill(glyph, s.fill, s.clip);
            }
            const auto found = f.widths.find(cid);
            const double advance =
                ((found == f.widths.end() ? f.default_width : found->second) * s.size / 1000 +
                 s.spacing + (f.cff && cid == 32 ? s.word_spacing : 0)) *
                s.hscale;
            s.text = s.text * Matrix{1, 0, 0, 1, advance, 0};
        }
    };
    std::function<void(const std::string &, const Value &, int)> execute;
    execute = [&](const std::string &contents, const Value &res, int depth) {
        check(depth < 32, "Form XObject circular");
        const auto &resources = document_.resolve(res);
        const auto &fonts = document_.resolve(resources["Font"]);
        const auto saved_depth = saved.size();
        Lexer lexer(contents);
        std::vector<Value> args;
        while (!lexer.done()) {
            Value token = lexer.value();
            if (token.kind != Value::Word) {
                check(args.size() < 10000, "Operandos de página excedidos");
                args.push_back(std::move(token));
                continue;
            }
            const auto &op = token.text;
            auto n = [&](std::size_t i) {
                check(i < args.size(), "Operandos insuficientes para " + op);
                return number(args[i]);
            };
            auto require = [&](std::size_t count) {
                check(args.size() == count, "Número de operandos incorreto para " + op);
            };
            auto matrix = [&]() {
                require(6);
                return Matrix{n(0), n(1), n(2), n(3), n(4), n(5)};
            };
            if (op == "q") {
                require(0);
                check(saved.size() < 128, "Pilha gráfica excedida");
                saved.push_back(s);
            } else if (op == "Q") {
                require(0);
                check(saved.size() > saved_depth, "Pilha gráfica vazia no conteúdo atual");
                s = saved.back();
                saved.pop_back();
            } else if (op == "cm")
                s.ctm = s.ctm * matrix();
            else if (op == "rg" || op == "RG") {
                require(3);
                (op == "rg" ? s.fill : s.stroke) = color(n(0), n(1), n(2));
            } else if (op == "g" || op == "G") {
                require(1);
                (op == "g" ? s.fill : s.stroke) = color(n(0), n(0), n(0));
            } else if (op == "k" || op == "K") {
                require(4);
                (op == "k" ? s.fill : s.stroke) =
                    color(1 - std::min(1.0, n(0) + n(3)), 1 - std::min(1.0, n(1) + n(3)),
                          1 - std::min(1.0, n(2) + n(3)));
            } else if (op == "gs") {
                require(1);
                const auto &state =
                    document_.resolve(document_.resolve(resources["ExtGState"])[args[0].text]);
                for (const auto &entry : state.dictionary) {
                    if (entry.first == "ca" || entry.first == "CA")
                        check(entry.second.number == 1, "Transparência ainda não suportada");
                    else if (entry.first == "BM") {
                        check(entry.second.is("Normal") || entry.second.is("Multiply"),
                              "Blend mode ainda não suportado");
                        s.clip.multiply = entry.second.is("Multiply");
                    } else if (entry.first == "LW")
                        s.line_width = number(entry.second);
                    else if (entry.first == "LC" || entry.first == "LJ") {
                        int v = entry.second.integer();
                        check(v >= 0 && v <= 2, "Estilo de linha inválido");
                        (entry.first == "LC" ? s.cap : s.join) = v;
                    } else if (entry.first == "ML")
                        s.miter = number(entry.second);
                    else if (entry.first == "AIS" || entry.first == "TK") {
                        check(entry.second.text == "true" ||
                                  (entry.first == "AIS" && entry.second.text == "false"),
                              "ExtGState não suportado: " + entry.first);
                    } else if (entry.first ==
                               "SA") { /* Stroke adjustment: aproximação por supersampling. */
                    } else if (entry.first == "OP" || entry.first == "op") {
                        check(entry.second.text == "false", "Overprint não suportado");
                    } else if (entry.first == "OPM") {
                        check(entry.second.integer() == 0 || entry.second.integer() == 1,
                              "OPM inválido");
                    } else if (entry.first == "SMask") {
                        if (entry.second.is("None"))
                            s.clip.softmask.reset();
                        else {
                            const auto &mask = document_.resolve(entry.second);
                            check(mask["S"].is("Luminosity") && mask["TR"].kind == Value::Null,
                                  "Softmask não suportada");
                            const auto &group = document_.object(mask["G"]);
                            State previous = s;
                            auto old_path = std::move(path);
                            bool old_clipping = clipping, old_even = clip_even_odd;
                            path.clear();
                            clipping = false;
                            Bitmap original = std::move(raster.bitmap);
                            raster.bitmap = Bitmap(original.width, original.height, 0);
                            const auto &bc = document_.resolve(mask["BC"]).array;
                            if (!bc.empty()) {
                                check(bc.size() == 1 || bc.size() == 3,
                                      "Backdrop softmask não suportado");
                                std::fill(raster.bitmap.pixels.begin(), raster.bitmap.pixels.end(),
                                          color(bc[0].number,
                                                bc.size() == 1 ? bc[0].number : bc[1].number,
                                                bc.size() == 1 ? bc[0].number : bc[2].number));
                            }
                            s.clip.softmask.reset();
                            s.clip.multiply = false;
                            const auto &m = document_.resolve(group.value["Matrix"]).array;
                            if (!m.empty()) {
                                check(m.size() == 6, "Matrix softmask inválida");
                                s.ctm = s.ctm * Matrix{number(m[0]), number(m[1]), number(m[2]),
                                                       number(m[3]), number(m[4]), number(m[5])};
                            }
                            const auto &bbox = document_.resolve(group.value["BBox"]).array;
                            check(bbox.size() == 4, "BBox softmask inválido");
                            const double x0 = number(bbox[0]), y0 = number(bbox[1]),
                                         x1 = number(bbox[2]), y1 = number(bbox[3]);
                            s.clip = raster.intersect({{s.ctm({x0, y0}), s.ctm({x1, y0}),
                                                        s.ctm({x1, y1}), s.ctm({x0, y1})}},
                                                      s.clip, false);
                            execute(std::string(view(document_.stream(mask["G"]))),
                                    group.value["Resources"], depth + 1);
                            auto alpha = std::make_shared<Bytes>(original.pixels.size());
                            for (std::size_t i = 0; i < alpha->size(); ++i) {
                                auto c = raster.bitmap.pixels[i];
                                (*alpha)[i] = static_cast<std::uint8_t>(0.3 * ((c >> 16) & 255) +
                                                                        0.59 * ((c >> 8) & 255) +
                                                                        0.11 * (c & 255) + 0.5);
                            }
                            raster.bitmap = std::move(original);
                            s = previous;
                            s.clip.softmask = alpha;
                            path = std::move(old_path);
                            clipping = old_clipping;
                            clip_even_odd = old_even;
                        }
                    } else if (entry.first != "Type")
                        throw std::runtime_error("ExtGState não suportado: " + entry.first);
                }
            } else if (op == "d") {
                require(2);
                check(args[0].kind == Value::Array, "Dash espera array");
                s.dash.clear();
                double sum = 0;
                for (const auto &v : args[0].array) {
                    double d = number(v);
                    check(d >= 0, "Dash negativo");
                    s.dash.push_back(d);
                    sum += d;
                }
                check(s.dash.empty() || sum > 0, "Dash vazio");
                s.dash_phase = n(1);
            } else if (op == "J" || op == "j") {
                require(1);
                int v = args[0].integer();
                check(v >= 0 && v <= 2, "Estilo de linha inválido");
                (op == "J" ? s.cap : s.join) = v;
            } else if (op == "M") {
                require(1);
                check(n(0) >= 1, "Miter limit inválido");
                s.miter = n(0);
            } else if (op == "w") {
                require(1);
                s.line_width = n(0);
            } else if (op == "re") {
                require(4);
                const double x = n(0), y = n(1), w = n(2), h = n(3);
                path.push_back({s.ctm({x, y}), s.ctm({x + w, y}), s.ctm({x + w, y + h}),
                                s.ctm({x, y + h}), s.ctm({x, y})});
            } else if (op == "m") {
                require(2);
                path.push_back({s.ctm({n(0), n(1)})});
            } else if (op == "l") {
                require(2);
                check(!path.empty(), "Linha sem início");
                path.back().push_back(s.ctm({n(0), n(1)}));
            } else if (op == "c" || op == "v" || op == "y") {
                require(op == "c" ? 6 : 4);
                check(!path.empty() && !path.back().empty(), "Curva sem início");
                const auto p = path.back().back();
                const auto a = op == "v" ? p : s.ctm({n(0), n(1)});
                const auto b = s.ctm({n(op == "v" ? 0 : 2), n(op == "v" ? 1 : 3)});
                const auto c = s.ctm({n(op == "c" ? 4 : 2), n(op == "c" ? 5 : 3)});
                for (int i = 1; i <= 16; ++i) {
                    const double t = i / 16.0, u = 1 - t;
                    path.back().push_back({u * u * u * p.x + 3 * u * u * t * a.x +
                                               3 * u * t * t * b.x + t * t * t * c.x,
                                           u * u * u * p.y + 3 * u * u * t * a.y +
                                               3 * u * t * t * b.y + t * t * t * c.y});
                }
            } else if (op == "h") {
                require(0);
                check(!path.empty() && !path.back().empty(), "Fechamento sem caminho");
                path.back().push_back(path.back().front());
            } else if (op == "W" || op == "W*") {
                require(0);
                clipping = true;
                clip_even_odd = op == "W*";
            } else if (op == "n") {
                require(0);
                end_path();
            } else if (op == "B" || op == "B*" || op == "b" || op == "b*") {
                require(0);
                if ((op == "b" || op == "b*") && !path.empty() && !path.back().empty())
                    path.back().push_back(path.back().front());
                raster.fill(path, s.fill, s.clip, op.back() == '*');
                stroke();
                end_path();
            } else if (op == "f" || op == "F" || op == "f*") {
                require(0);
                raster.fill(path, s.fill, s.clip, op == "f*");
                end_path();
            } else if (op == "S" || op == "s") {
                require(0);
                if (op == "s" && !path.empty() && !path.back().empty())
                    path.back().push_back(path.back().front());
                stroke();
                end_path();
            } else if (op == "BT") {
                require(0);
                s.text = s.line = Matrix{};
            } else if (op == "ET")
                require(0);
            else if (op == "Tf") {
                require(2);
                s.font = args[0].text;
                s.size = n(1);
            } else if (op == "Tm")
                s.text = s.line = matrix();
            else if (op == "Td" || op == "TD") {
                require(2);
                if (op == "TD")
                    s.leading = -n(1);
                s.text = s.line = s.line * Matrix{1, 0, 0, 1, n(0), n(1)};
            } else if (op == "T*") {
                require(0);
                s.text = s.line = s.line * Matrix{1, 0, 0, 1, 0, -s.leading};
            } else if (op == "Tj") {
                require(1);
                text(args[0], fonts);
            } else if (op == "TJ") {
                require(1);
                check(args[0].kind == Value::Array, "TJ espera array");
                for (const auto &v : args[0].array) {
                    if (v.kind == Value::Number)
                        s.text =
                            s.text * Matrix{1, 0, 0, 1, -v.number * s.size * s.hscale / 1000, 0};
                    else
                        text(v, fonts);
                }
            } else if (op == "Tc") {
                require(1);
                s.spacing = n(0);
            } else if (op == "Tw") {
                require(1);
                s.word_spacing = n(0);
            } else if (op == "Tz") {
                require(1);
                s.hscale = n(0) / 100;
            } else if (op == "TL") {
                require(1);
                s.leading = n(0);
            } else if (op == "Ts") {
                require(1);
                s.rise = n(0);
            } else if (op == "Tr") {
                require(1);
                s.text_mode = args[0].integer();
            } else if (op == "Do") {
                require(1);
                const auto &ref = document_.resolve(resources["XObject"])[args[0].text];
                const auto &obj = document_.object(ref);
                if (obj.value["Subtype"].is("Form")) {
                    State previous = s;
                    auto previous_path = std::move(path);
                    const bool previous_clipping = clipping, previous_even = clip_even_odd;
                    path.clear();
                    clipping = false;
                    const auto &m = document_.resolve(obj.value["Matrix"]);
                    if (m.kind != Value::Null) {
                        check(m.array.size() == 6, "Matrix de Form inválida");
                        s.ctm = s.ctm * Matrix{number(m.array[0]), number(m.array[1]),
                                               number(m.array[2]), number(m.array[3]),
                                               number(m.array[4]), number(m.array[5])};
                    }
                    const auto &b = document_.resolve(obj.value["BBox"]).array;
                    check(b.size() == 4, "BBox de Form inválido");
                    double x0 = number(b[0]), y0 = number(b[1]), x1 = number(b[2]),
                           y1 = number(b[3]);
                    s.clip = raster.intersect(
                        {{s.ctm({x0, y0}), s.ctm({x1, y0}), s.ctm({x1, y1}), s.ctm({x0, y1})}},
                        s.clip, false);
                    execute(std::string(view(document_.stream(ref))),
                            obj.value["Resources"].kind == Value::Null ? res
                                                                       : obj.value["Resources"],
                            depth + 1);
                    s = previous;
                    path = std::move(previous_path);
                    clipping = previous_clipping;
                    clip_even_odd = previous_even;
                } else {
                    const auto image = decode_image(document_, ref, res, s.fill);
                    raster.image(image.bitmap, s.ctm, s.clip, image.alpha);
                }
            } else if (op == "BDC" || op == "BMC" || op == "EMC" || op == "MP" ||
                       op == "DP") { /* Metadados de acessibilidade não alteram a pintura. */
            } else
                throw std::runtime_error("Operador PDF ainda não suportado: " + op);
            args.clear();
        }
        check(args.empty() && saved.size() == saved_depth, "Conteúdo PDF incompleto");
    };
    execute(document_.content(page.contents), page.resources, 0);
    return raster.downsample();
}
} // namespace pdf
