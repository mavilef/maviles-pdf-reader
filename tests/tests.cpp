#include "deflate_fixtures.h"
#include "fax.h"
#include "inflate.h"
#include "jpeg.h"
#include "raster.h"
#include "render.h"
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <unistd.h>

namespace {
using namespace pdf;
void require(bool ok, const std::string &message) {
    check(ok, "TESTE: " + message);
}
template <class F> void rejects(F function) {
    bool failed = false;
    try {
        function();
    } catch (const std::exception &) {
        failed = true;
    }
    require(failed, "entrada inválida foi aceita");
}
Bytes unhex(std::string_view s) {
    Bytes bytes;
    for (std::size_t i = 0; i < s.size(); i += 2)
        bytes.push_back(
            static_cast<std::uint8_t>(std::stoul(std::string(s.substr(i, 2)), nullptr, 16)));
    return bytes;
}
void test_inflate() {
    const auto stored = inflate(view(unhex(fixtures::stored)));
    const std::string expected("PDF sem bibliotecas\0\xff", 21);
    require(view(stored) == expected, "bloco DEFLATE sem compressão");
    std::string repeated;
    for (int i = 0; i < 1000; ++i)
        repeated += "abcde";
    require(view(inflate(view(unhex(fixtures::fixed)))) == repeated,
            "Huffman fixo e referências sobrepostas");
    Bytes original;
    for (int i = 0; i < 10000; ++i)
        original.push_back((i * i / 7 + i * 13) % 251);
    const auto compressed = unhex(fixtures::dynamic);
    require(inflate(view(compressed)) == original, "Huffman dinâmico");
    for (std::size_t n = 0; n < compressed.size(); n += 17)
        rejects([&] { inflate(view(compressed).substr(0, n)); });
    auto corrupted = compressed;
    corrupted.back() ^= 1;
    rejects([&] { inflate(view(corrupted)); });
    corrupted[0] = 0;
    rejects([&] { inflate(view(corrupted)); });
}
void test_lexer() {
    Lexer lexer("% comentario\n<< /N#61me (a\\(b\\)\\n\\101) /A [12 7 R -2.5 <a b c>] >>");
    const auto v = lexer.value();
    require(v["Name"].text == "a(b)\nA", "escapes de strings/nomes");
    const auto &a = v["A"].array;
    require(a.size() == 3 && a[0].kind == Value::Reference && a[0].id == 12 && a[0].generation == 7,
            "referências PDF");
    require(a[1].number == -2.5 && a[2].text == std::string("\xab\xc0", 2),
            "número e hexadecimal ímpar");
    require(lexer.done(), "fim do lexer");
    for (const auto *s : {"[1 2", "(abc", "<ab", "<< /A >>", "/A#z1"})
        rejects([&] {
            Lexer input(s);
            input.value();
        });
}
void be16(Bytes &b, unsigned n) {
    b.push_back((n >> 8) & 255);
    b.push_back(n & 255);
}
void set16(Bytes &b, std::size_t p, unsigned n) {
    b.at(p) = (n >> 8) & 255;
    b.at(p + 1) = n & 255;
}
void set32(Bytes &b, std::size_t p, unsigned n) {
    set16(b, p, n >> 16);
    set16(b, p + 2, n & 65535);
}
Bytes simple_font() {
    Bytes head(54), maxp(6), glyf, loca;
    set16(head, 18, 1000);
    set16(head, 50, 0);
    set16(maxp, 4, 2);
    be16(glyf, 1);
    for (auto n : {0, 0, 1000, 1000})
        be16(glyf, n);
    be16(glyf, 3);
    be16(glyf, 0);
    glyf.insert(glyf.end(), 4, 1);
    for (auto n : {0, 1000, 0, -1000, 0, 0, 1000, 0})
        be16(glyf, static_cast<unsigned>(n));
    const unsigned second = glyf.size();
    be16(glyf, 65535);
    for (auto n : {100, 200, 1100, 1200})
        be16(glyf, n);
    be16(glyf, 3);
    be16(glyf, 0);
    be16(glyf, 100);
    be16(glyf, 200);
    for (unsigned n : {0u, second, static_cast<unsigned>(glyf.size())})
        be16(loca, n / 2);
    std::vector<std::pair<std::string, Bytes>> tables = {
        {"head", head}, {"maxp", maxp}, {"glyf", glyf}, {"loca", loca}};
    Bytes result(12 + tables.size() * 16);
    set32(result, 0, 0x10000);
    set16(result, 4, tables.size());
    for (std::size_t i = 0; i < tables.size(); ++i) {
        const auto p = 12 + i * 16;
        const auto &entry = tables[i];
        std::copy(entry.first.begin(), entry.first.end(), result.begin() + p);
        set32(result, p + 8, result.size());
        set32(result, p + 12, entry.second.size());
        result.insert(result.end(), entry.second.begin(), entry.second.end());
    }
    return result;
}
void test_font() {
    const auto bytes = simple_font();
    TrueType font(bytes);
    const auto &simple = font.glyph(0);
    const auto &composite = font.glyph(1);
    require(font.units() == 1000 && simple.size() == 1 && simple[0].size() == 5,
            "contorno TrueType simples");
    require(composite[0][0].x == simple[0][0].x + 100 && composite[0][0].y == simple[0][0].y + 200,
            "glifo composto e deslocamento");
    rejects([&] { font.glyph(2); });
    for (std::size_t n = 0; n < bytes.size(); n += 7)
        rejects([&] {
            TrueType broken(Bytes(bytes.begin(), bytes.begin() + n));
            broken.glyph(0);
        });
}
Bytes gray_jpeg(bool restart, int components = 1, int adobe = -1) {
    Bytes b{255, 216};
    auto segment = [&](unsigned marker, const Bytes &data) {
        b.push_back(255);
        b.push_back(marker);
        be16(b, data.size() + 2);
        b.insert(b.end(), data.begin(), data.end());
    };
    if (adobe >= 0)
        segment(0xee,
                {'A', 'd', 'o', 'b', 'e', 0, 100, 0, 0, 0, 0, static_cast<std::uint8_t>(adobe)});
    Bytes quant(65, 1);
    quant[0] = 0;
    segment(0xdb, quant);
    Bytes frame{8,
                0,
                8,
                0,
                static_cast<std::uint8_t>(restart ? 16 : 8),
                static_cast<std::uint8_t>(components)};
    for (int i = 1; i <= components; ++i)
        frame.insert(frame.end(), {static_cast<std::uint8_t>(i), 0x11, 0});
    segment(0xc0, frame);
    Bytes huff{0, 1};
    huff.insert(huff.end(), 15, 0);
    huff.push_back(0);
    huff.push_back(0x10);
    huff.push_back(1);
    huff.insert(huff.end(), 15, 0);
    huff.push_back(0);
    segment(0xc4, huff);
    if (restart)
        segment(0xdd, {0, 1});
    Bytes scan{static_cast<std::uint8_t>(components)};
    for (int i = 1; i <= components; ++i)
        scan.insert(scan.end(), {static_cast<std::uint8_t>(i), 0});
    scan.insert(scan.end(), {0, 63, 0});
    segment(0xda, scan);
    b.push_back(components == 4 ? 0 : 0x3f);
    if (restart)
        b.insert(b.end(), {255, 208, 0x3f});
    b.insert(b.end(), {255, 217});
    return b;
}
void test_jpeg() {
    for (int adobe : {-1, 0, 2}) {
        auto image = decode_jpeg(view(gray_jpeg(false, 4, adobe)));
        auto expected = adobe >= 0 ? 0x404040u : 0x3f3f3fu;
        require(image.pixels[0] == expected, "JPEG CMYK/YCCK e inversão Adobe");
    }
    for (bool restart : {false, true}) {
        const auto bytes = gray_jpeg(restart);
        const auto image = decode_jpeg(view(bytes));
        require(image.width == (restart ? 16 : 8) && image.height == 8, "dimensões JPEG");
        require(std::all_of(image.pixels.begin(), image.pixels.end(),
                            [](auto p) { return p == 0x808080; }),
                "IDCT DC e restart JPEG");
        for (std::size_t n = 0; n + 4 < bytes.size(); n += 7)
            rejects([&] { decode_jpeg(view(bytes).substr(0, n)); });
        rejects([&] { decode_jpeg(view(bytes).substr(0, bytes.size() - 1)); });
    }
}
void test_raster() {
    const Clip clip{0, 0, 100, 100};
    Path rings{{{10, 10}, {90, 10}, {90, 90}, {10, 90}}, {{30, 30}, {70, 30}, {70, 70}, {30, 70}}};
    Raster nonzero(100, 100);
    nonzero.fill(rings, 0, clip);
    require(nonzero.bitmap.pixels[5050] == 0, "preenchimento nonzero");
    Raster evenodd(100, 100);
    evenodd.fill(rings, 0, clip, true);
    require(evenodd.bitmap.pixels[5050] == 0xffffff && evenodd.bitmap.pixels[2020] == 0,
            "preenchimento even-odd com buraco");
    std::reverse(rings[1].begin(), rings[1].end());
    Raster reversed(100, 100);
    reversed.fill(rings, 0, {20, 20, 80, 80});
    require(reversed.bitmap.pixels[5050] == 0xffffff && reversed.bitmap.pixels[2525] == 0 &&
                reversed.bitmap.pixels[1515] == 0xffffff,
            "orientação dos contornos e recorte");
    Bitmap source(2, 1);
    source.pixels = {0xff0000, 0x0000ff};
    Raster image(30, 30);
    image.image(source, {20, 0, 0, 10, 3, 4}, {0, 0, 30, 30});
    require(image.bitmap.pixels[5 * 30 + 5] == 0xff0000 &&
                image.bitmap.pixels[5 * 30 + 15] == 0x0000ff && image.bitmap.pixels[0] == 0xffffff,
            "transformação de imagem e orientação PDF");
}
struct Temporary {
    std::filesystem::path folder;
    Temporary() {
        char name[] = "/tmp/leitor-pdf-test-XXXXXX";
        const char *p = mkdtemp(name);
        check(p, "mkdtemp falhou");
        folder = p;
    }
    ~Temporary() {
        std::error_code error;
        std::filesystem::remove_all(folder, error);
    }
};
std::string make_pdf(const std::string &second, bool incremental) {
    auto stream = [](const std::string &s) {
        return "<< /Length " + std::to_string(s.size()) + " >>\nstream\n" + s + "\nendstream";
    };
    std::vector<std::string> objects = {
        "<< /Type /Catalog /Pages 2 0 R >>",
        "<< /Type /Pages /Count 2 /Kids 7 0 R /MediaBox [0 0 100 100] /Resources << >> >>",
        "<< /Type /Page /Parent 2 0 R /Contents 5 0 R >>",
        "<< /Type /Page /Parent 2 0 R /Contents [6 0 R] >>",
        stream("1 0 0 rg 10 10 20 20 re f"),
        stream(second),
        "[3 0 R 4 0 R]"};
    std::string data = "%PDF-1.4\n";
    std::vector<std::size_t> offsets{0};
    for (std::size_t i = 0; i < objects.size(); ++i) {
        offsets.push_back(data.size());
        data += std::to_string(i + 1) + " 0 obj\n" + objects[i] + "\nendobj\n";
    }
    const auto xref = data.size();
    data += "xref\n0 8\n0000000000 65535 f \n";
    for (std::size_t i = 1; i < offsets.size(); ++i) {
        std::ostringstream line;
        line << std::setfill('0') << std::setw(10) << offsets[i] << " 00000 n \n";
        data += line.str();
    }
    data += "trailer\n<< /Size 8 /Root 1 0 R >>\nstartxref\n" + std::to_string(xref) + "\n%%EOF\n";
    if (incremental) {
        const auto updated = data.size();
        data += "5 0 obj\n" + stream("0 1 0 rg 10 10 20 20 re f") + "\nendobj\n";
        const auto last = data.size();
        std::ostringstream line;
        line << std::setfill('0') << std::setw(10) << updated << " 00000 n \n";
        data += "xref\n5 1\n" + line.str() + "trailer\n<< /Size 8 /Prev " + std::to_string(xref) +
                " >>\nstartxref\n" + std::to_string(last) + "\n%%EOF\n";
    }
    return data;
}
Bytes bit_bytes(const std::string &bits) {
    Bytes b((bits.size() + 7) / 8);
    for (std::size_t i = 0; i < bits.size(); ++i)
        if (bits[i] == '1')
            b[i / 8] |= 1 << (7 - i % 8);
    return b;
}
void test_fax() {
    require(decode_fax4(view(bit_bytes("11")), 8, 2) == Bytes(16),
            "fax branco, referência vertical");
    // Horizontal: branco 0, preto 8; segunda linha repete ambas as transições.
    auto black = bit_bytes("0010011010100010111");
    require(decode_fax4(view(black), 8, 2) == Bytes(16, 1), "fax preto e linha de referência");
    const auto mixed = decode_fax4(view(bit_bytes("001101101111")), 8, 2);
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 8; ++x)
            require(mixed[y * 8 + x] == (x >= 4), "fax horizontal/vertical");
    rejects([&] { decode_fax4({}, 8, 1); });
    rejects([&] { decode_fax4(view(bit_bytes("0000000")), 8, 1); });
    rejects([&] { decode_fax4(view(black), 7, 1); });
}
void cff_index(Bytes &out, const std::vector<Bytes> &items) {
    be16(out, items.size());
    if (items.empty())
        return;
    out.push_back(1);
    unsigned offset = 1;
    out.push_back(offset);
    for (const auto &b : items) {
        offset += b.size();
        require(offset < 256, "fixture INDEX curta");
        out.push_back(offset);
    }
    for (const auto &b : items)
        out.insert(out.end(), b.begin(), b.end());
}
void test_cff() {
    Bytes font{1, 0, 4, 4};
    cff_index(font, {{'T'}});
    // Top DICT de tamanho fixo; offset CharStrings preenchido abaixo.
    Bytes dict{29, 0, 0, 0, 0, 17};
    const auto dict_position = font.size() + 5;
    cff_index(font, {dict});
    cff_index(font, {});
    // Quadrado 100x100 na subrotina global 0.
    cff_index(font, {{239, 139, 139, 239, 39, 139, 139, 39, 5, 11}});
    set32(font, dict_position + 1, font.size());
    cff_index(font, {{14}, {139, 139, 21, 32, 29, 14}});
    CFF face(font);
    require(face.named("space") == 1 && face.encoded(32) == 1, "charset/encoding CFF");
    Raster raster(120, 120);
    raster.fill(face.glyph(1), 0, {0, 0, 120, 120});
    require(raster.bitmap.pixels[50 * 120 + 50] == 0 &&
                raster.bitmap.pixels[110 * 120 + 110] == 0xffffff,
            "Type2 subrotina e contorno");
    require(face.glyph(0).empty(), "CFF glifo vazio");
    rejects([&] { face.glyph(2); });
    for (std::size_t n = 0; n < font.size(); ++n)
        rejects([&] {
            CFF bad(Bytes(font.begin(), font.begin() + n));
            bad.glyph(1);
        });
    auto circular = font;
    circular[circular.size() - 3] = 32;
    circular[circular.size() - 2] = 10;
    rejects([&] {
        CFF bad(circular);
        bad.glyph(1);
    });
}
std::string pdf_stream(const std::string &s, const std::string &extra = "") {
    return "<< /Length " + std::to_string(s.size()) + " " + extra + " >>\rstream\r" + s +
           "\rendstream";
}
std::string objects_pdf(const std::vector<std::string> &objects) {
    std::string s = "%PDF-1.5\n";
    std::vector<std::size_t> offsets;
    for (std::size_t i = 0; i < objects.size(); ++i) {
        offsets.push_back(s.size());
        s += std::to_string(i + 1) + " 0 obj\n" + objects[i] + "\nendobj\n";
    }
    auto x = s.size();
    s += "xref\n0 " + std::to_string(objects.size() + 1) + "\n0000000000 65535 f \n";
    for (auto off : offsets) {
        std::ostringstream line;
        line << std::setfill('0') << std::setw(10) << off << " 00000 n \n";
        s += line.str();
    }
    return s + "trailer << /Size " + std::to_string(objects.size() + 1) +
           " /Root 1 0 R >>\nstartxref\n" + std::to_string(x) + "\n%%EOF\n";
}
std::string stored_zlib(const std::string &s) {
    require(s.size() < 65536, "fixture zlib curta");
    Bytes b{0x78, 1, 1};
    unsigned len = s.size();
    b.push_back(len & 255);
    b.push_back(len >> 8);
    b.push_back((~len) & 255);
    b.push_back(((~len) >> 8) & 255);
    b.insert(b.end(), s.begin(), s.end());
    unsigned a = 1, c = 0;
    for (unsigned char v : s) {
        a = (a + v) % 65521;
        c = (c + a) % 65521;
    }
    b.push_back(c >> 8);
    b.push_back(c & 255);
    b.push_back(a >> 8);
    b.push_back(a & 255);
    return std::string(view(b));
}
std::string packed_pdf(int fault = 0, bool incremental = false) {
    std::string s = "%PDF-1.5\n";
    const std::vector<std::string> items = {
        "<< /Type /Catalog /Pages 2 0 R >>",
        "<< /Type /Pages /Count 1 /Kids [3 0 R] /MediaBox [0 0 100 100] >>",
        "<< /Type /Page /Parent 2 0 R /Contents 5 0 R >>"};
    std::string header, body;
    for (int i = 0; i < 3; ++i) {
        header += std::to_string(i + 1) + " " + std::to_string(body.size()) + " ";
        body += items[i] + " ";
    }
    auto four = s.size();
    s += "4 0 obj\n" +
         pdf_stream(stored_zlib(header + body), "/Type /ObjStm /N 3 /First " +
                                                    std::to_string(header.size()) +
                                                    " /Filter /FlateDecode") +
         "\nendobj\n";
    auto five = s.size();
    s += "5 0 obj\n" + pdf_stream("1 0 0 rg 0 0 100 100 re f") + "\nendobj\n";
    auto six = s.size();
    Bytes entries;
    auto entry = [&](int type, unsigned offset, int gen) {
        entries.push_back(type);
        entries.push_back(offset >> 16);
        entries.push_back(offset >> 8);
        entries.push_back(offset);
        entries.push_back(gen);
    };
    entry(0, 0, 0);
    entry(2, 4, fault == 1 ? 2 : 0);
    entry(2, 4, 1);
    entry(2, 4, 2);
    entry(1, four, 0);
    entry(1, five, 0);
    entry(1, six, 0);
    std::string predicted;
    for (std::size_t row = 0; row < 7; ++row) {
        predicted += char(2);
        for (int j = 0; j < 5; ++j)
            predicted += char(entries[row * 5 + j] - (row ? entries[(row - 1) * 5 + j] : 0));
    }
    if (fault == 2)
        predicted.pop_back();
    s += "6 0 obj\n" +
         pdf_stream(stored_zlib(predicted),
                    "/Type /XRef /Size 7 /Root 1 0 R /W [1 3 1] /Filter /FlateDecode /DecodeParms "
                    "<< /Predictor 12 /Columns 5 >>") +
         "\nendobj\nstartxref\n" + std::to_string(six) + "\n%%EOF\n";
    if (incremental) {
        auto updated = s.size();
        s += "5 0 obj\n" + pdf_stream("0 1 0 rg 0 0 100 100 re f") + "\nendobj\n";
        auto next = s.size();
        entries.clear();
        entry(1, updated, 0);
        s +=
            "7 0 obj\n" +
            pdf_stream(std::string(view(entries)),
                       "/Type /XRef /Size 8 /Index [5 1] /W [1 3 1] /Prev " + std::to_string(six)) +
            "\nendobj\nstartxref\n" + std::to_string(next) + "\n%%EOF\n";
    }
    return s;
}
void test_packed_document() {
    Temporary temp;
    auto file = temp.folder / "packed.pdf";
    for (bool incremental : {false, true}) {
        std::ofstream(file, std::ios::binary) << packed_pdf(0, incremental);
        Document d(file.string());
        Renderer r(d);
        require(d.pages().size() == 1, "páginas compactadas");
        require(r.render(0).pixels[5050] == (incremental ? 0x00ff00u : 0xff0000u),
                "xref stream PNG Up / ObjStm / Prev");
    }
    for (int fault : {1, 2}) {
        std::ofstream(file, std::ios::binary) << packed_pdf(fault);
        rejects([&] { Document d(file.string()); });
    }
}
void test_forms_images() {
    Temporary temp;
    auto file = temp.folder / "forms.pdf";
    const std::vector<std::string> objects = {
        "<< /Type /Catalog /Pages 2 0 R >>",
        "<< /Type /Pages /Count 1 /Kids [3 0 R] /MediaBox [0 0 100 100] >>",
        "<< /Type /Page /Parent 2 0 R /Resources << /XObject << /F 5 0 R >> >> /Contents 4 0 R >>",
        pdf_stream("q 1 0 0 1 20 20 cm /F Do Q 0 0 1 rg 0 0 10 10 re f"),
        pdf_stream("q 20 0 0 20 0 0 cm /I Do Q",
                   "/Type /XObject /Subtype /Form /BBox [0 0 10 20] /Matrix [1 0 0 1 5 0] "
                   "/Resources << /XObject << /I 6 0 R >> >>"),
        pdf_stream(std::string("\xff\0\0", 3), "/Subtype /Image /Width 1 /Height 1 /ColorSpace "
                                               "/DeviceRGB /BitsPerComponent 8 /SMask 7 0 R"),
        pdf_stream(
            std::string(1, char(128)),
            "/Subtype /Image /Width 1 /Height 1 /ColorSpace /DeviceGray /BitsPerComponent 8")};
    std::ofstream(file, std::ios::binary) << objects_pdf(objects);
    Document d(file.string());
    Renderer r(d);
    auto image = r.render(0);
    require(image.pixels[70 * 100 + 28] == 0xff7f7f, "Form Matrix e imagem com alpha");
    require(image.pixels[70 * 100 + 38] == 0xffffff, "Form BBox recorta imagem");
    require(image.pixels[95 * 100 + 5] == 0x0000ff, "Form restaura transformação/recorte");
    auto indexed = objects;
    indexed[5] =
        pdf_stream(std::string(1, '\0'), "/Subtype /Image /Width 1 /Height 1 /ColorSpace [/Indexed "
                                         "/DeviceRGB 0 <ff0000>] /BitsPerComponent 8 /SMask 7 0 R");
    std::ofstream(file, std::ios::binary) << objects_pdf(indexed);
    Document indexed_document(file.string());
    Renderer indexed_renderer(indexed_document);
    require(indexed_renderer.render(0).pixels[70 * 100 + 28] == 0xff7f7f,
            "Imagem Indexed e paleta direta");
    Raster raster(20, 20);
    Clip clip{0, 0, 20, 20};
    auto triangle = raster.intersect({{{0, 0}, {20, 0}, {0, 20}}}, clip, false);
    raster.fill({{{0, 0}, {20, 0}, {20, 20}, {0, 20}}}, 0, triangle);
    require(raster.bitmap.pixels[2 * 20 + 2] == 0 && raster.bitmap.pixels[18 * 20 + 18] == 0xffffff,
            "recorte triangular");
    Raster dashed(40, 10);
    dashed.stroke({{{0, 5}, {40, 5}}}, 2, 0, {0, 0, 40, 10}, 0, 0, 4, {5, 5});
    require(dashed.bitmap.pixels[5 * 40 + 2] == 0 && dashed.bitmap.pixels[5 * 40 + 7] == 0xffffff,
            "traço pontilhado");
}

void test_document() {
    Temporary temporary;
    const auto file = temporary.folder / "teste com espaços e acentos á.pdf";
    for (bool incremental : {false, true}) {
        std::ofstream(file, std::ios::binary) << make_pdf("0 0 1 rg 0 0 100 100 re f", incremental);
        Document document(file.string());
        Renderer renderer(document);
        require(document.pages().size() == 2, "ordem e herança da árvore de páginas");
        const auto image = renderer.render(0);
        require(image.width == 100 && image.height == 100, "tamanho da página");
        require(image.pixels[80 * 100 + 20] == (incremental ? 0x00ff00u : 0xff0000u),
                "renderização e xref incremental");
        require(image.pixels[0] == 0xffffff, "fundo branco");
        const auto blue = renderer.render(1);
        require(blue.pixels[5050] == 0x0000ff, "array de conteúdos e segunda página");
        rejects([&] { renderer.render(2); });
        rejects([&] { renderer.render(0, -1); });
    }
    std::ofstream(file) << make_pdf("BI", false);
    Document unsupported(file.string());
    Renderer r(unsupported);
    rejects([&] { r.render(1); });
    std::ofstream(file) << "não é PDF";
    rejects([&] { Document broken(file.string()); });
    rejects([&] { Document missing((temporary.folder / "ausente.pdf").string()); });
}
} // namespace
int main() {
    try {
        test_inflate();
        test_lexer();
        test_font();
        test_jpeg();
        test_raster();
        test_document();
        test_cff();
        test_fax();
        test_packed_document();
        test_forms_images();
        std::cout << "OK: DEFLATE, lexer, TrueType, JPEG/CMYK, CFF, fax, xref/ObjStm, Form, "
                     "máscaras e renderização\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
