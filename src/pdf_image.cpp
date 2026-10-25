#include "pdf_image.h"
#include "fax.h"
#include "jpeg.h"
namespace pdf {
PDFImage decode_image(Document &d, const Value &ref, const Value &resources, std::uint32_t stencil,
                      int depth) {
    check(depth < 16, "Máscara de imagem circular");
    const auto &o = d.object(ref);
    const auto &v = o.value;
    check(v["Subtype"].is("Image"), "XObject não é imagem");
    const int w = d.resolve(v["Width"]).integer(), h = d.resolve(v["Height"]).integer();
    check(w > 0 && h > 0 && std::uint64_t(w) * h <= 64 * 1024 * 1024, "Imagem grande/inválida");
    PDFImage out;
    bool mask = v["ImageMask"].text == "true";
    const auto &filter = d.resolve(v["Filter"]);
    const auto &decode = d.resolve(v["Decode"]).array;
    if (filter.is("DCTDecode")) {
        out.bitmap = decode_jpeg(o.stream);
        check(out.bitmap.width == w && out.bitmap.height == h, "Dimensões JPEG divergentes");
        check(decode.empty() ||
                  (decode.size() == 2 && decode[0].number == 0 && decode[1].number == 1),
              "Decode JPEG não suportado");
    } else if (filter.is("CCITTFaxDecode")) {
        const auto &params = d.resolve(v["DecodeParms"]);
        check(params["K"].integer() < 0 && params["Columns"].integer() == w &&
                  (params["Rows"].kind == Value::Null || params["Rows"].integer() == h) &&
                  params["EncodedByteAlign"].text != "true",
              "Parâmetros fax não suportados");
        auto pixels = decode_fax4(o.stream, w, h);
        out.bitmap = Bitmap(w, h, stencil);
        if (mask)
            out.alpha.resize(pixels.size());
        check(decode.empty() ||
                  (decode.size() == 2 && (decode[0].number == 0 || decode[0].number == 1) &&
                   decode[1].number == 1 - decode[0].number),
              "Decode fax inválido");
        for (std::size_t i = 0; i < pixels.size(); ++i) {
            bool sample = params["BlackIs1"].text == "true" ? pixels[i] : !pixels[i];
            if (!decode.empty() && decode[0].number == 1)
                sample = !sample;
            if (mask)
                out.alpha[i] = sample ? 0 : 255;
            else
                out.bitmap.pixels[i] = sample ? 0xffffff : 0;
        }
    } else {
        check(filter.kind == Value::Null || filter.is("FlateDecode"),
              "Filtro de imagem não suportado: " + filter.text);
        int bits = mask ? 1 : d.resolve(v["BitsPerComponent"]).integer();
        check(bits == 1 || bits == 2 || bits == 4 || bits == 8, "BitsPerComponent não suportado");
        Value cs = d.resolve(v["ColorSpace"]);
        if (cs.kind == Value::Name && !cs.is("DeviceGray") && !cs.is("DeviceRGB") &&
            !cs.is("DeviceCMYK"))
            cs = d.resolve(d.resolve(d.resolve(resources)["ColorSpace"])[cs.text]);
        bool indexed = cs.kind == Value::Array && !cs.array.empty() && cs.array[0].is("Indexed");
        Bytes palette;
        int hi = 0;
        if (indexed) {
            check(cs.array.size() == 4, "Indexed inválido");
            hi = d.resolve(cs.array[2]).integer();
            check(hi >= 0 && hi <= 255, "Indexed excedido");
            const auto &lookup = cs.array[3];
            if (lookup.kind == Value::Reference)
                palette = d.stream(lookup);
            else {
                const auto &str = d.resolve(lookup);
                check(str.kind == Value::String, "Palette inválida");
                palette.assign(str.text.begin(), str.text.end());
            }
            Value base = d.resolve(cs.array[1]);
            cs = std::move(base);
        }
        if (cs.kind == Value::Array && cs.array.size() == 2 && cs.array[0].is("ICCBased")) {
            const auto &icc = d.resolve(cs.array[1]);
            if (icc["Alternate"].kind != Value::Null)
                cs = d.resolve(icc["Alternate"]);
            else {
                int n = icc["N"].integer();
                cs.kind = Value::Name;
                cs.text = n == 1 ? "DeviceGray" : n == 3 ? "DeviceRGB" : n == 4 ? "DeviceCMYK" : "";
            }
        }
        const int channels = mask                  ? 1
                             : cs.is("DeviceGray") ? 1
                             : cs.is("DeviceRGB")  ? 3
                             : cs.is("DeviceCMYK") ? 4
                                                   : 0;
        check(channels > 0, "ColorSpace de imagem não suportado");
        if (indexed)
            check(palette.size() >= static_cast<std::size_t>((hi + 1) * channels),
                  "Palette truncada");
        const auto data = d.stream(ref);
        const int samples = indexed ? 1 : channels, max = (1 << bits) - 1;
        std::size_t stride = (static_cast<std::size_t>(w) * samples * bits + 7) / 8;
        check(data.size() == stride * h, "Comprimento de imagem inválido");
        check(decode.empty() || decode.size() == std::size_t(samples * 2), "Decode inválido");
        out.bitmap = Bitmap(w, h, stencil);
        if (mask)
            out.alpha.resize(std::size_t(w) * h);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                double components[4]{};
                for (int c = 0; c < samples; ++c) {
                    std::size_t bit = std::size_t(x * samples + c) * bits;
                    int raw = (data[y * stride + bit / 8] >> (8 - bits - bit % 8)) & max;
                    double lo = decode.empty() ? 0 : decode[c * 2].number,
                           up = decode.empty() ? (indexed ? max : 1) : decode[c * 2 + 1].number;
                    double value = lo + raw * (up - lo) / max;
                    if (indexed) {
                        int at = std::clamp(int(std::lround(value)), 0, hi);
                        for (int j = 0; j < channels; ++j)
                            components[j] = palette[at * channels + j] / 255.0;
                    } else
                        components[c] = std::clamp(value, 0.0, 1.0);
                }
                const auto p = std::size_t(y) * w + x;
                if (mask) {
                    out.alpha[p] = static_cast<std::uint8_t>(255 * (1 - components[0]));
                    continue;
                }
                double r = components[0], g = channels == 1 ? r : components[1],
                       b = channels == 1 ? r : components[2];
                if (channels == 4) {
                    r = (1 - r) * (1 - components[3]);
                    g = (1 - g) * (1 - components[3]);
                    b = (1 - b) * (1 - components[3]);
                }
                out.bitmap.pixels[p] = (unsigned(r * 255 + 0.5) << 16) |
                                       (unsigned(g * 255 + 0.5) << 8) | unsigned(b * 255 + 0.5);
            }
    }
    check(v["Mask"].kind == Value::Null, "Máscara de cor não suportada");
    if (v["SMask"].kind != Value::Null) {
        auto alpha = decode_image(d, v["SMask"], resources, 0, depth + 1);
        check(alpha.alpha.empty(), "SMask inválida");
        out.alpha.resize(std::size_t(w) * h);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
                out.alpha[std::size_t(y) * w + x] =
                    alpha.bitmap
                        .pixels[std::size_t(y) * alpha.bitmap.height / h * alpha.bitmap.width +
                                std::size_t(x) * alpha.bitmap.width / w] &
                    255;
    }
    return out;
}
} // namespace pdf
