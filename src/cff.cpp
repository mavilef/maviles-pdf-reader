#include "cff.h"
#include <functional>
#include <sstream>
namespace pdf {
namespace {
unsigned byte(std::string_view s, std::size_t &p) {
    check(p < s.size(), "CFF truncado");
    return static_cast<unsigned char>(s[p++]);
}
unsigned uintn(std::string_view s, std::size_t &p, int n) {
    unsigned v = 0;
    while (n--)
        v = v * 256 + byte(s, p);
    return v;
}
double number(std::string_view s, std::size_t &p, int b, bool dict) {
    if (b >= 32 && b <= 246)
        return b - 139;
    if (b >= 247 && b <= 250)
        return (b - 247) * 256 + byte(s, p) + 108;
    if (b >= 251 && b <= 254)
        return -(b - 251) * 256 - int(byte(s, p)) - 108;
    if (b == 28)
        return static_cast<std::int16_t>(uintn(s, p, 2));
    if (b == 29 && dict)
        return static_cast<std::int32_t>(uintn(s, p, 4));
    if (b == 255 && !dict)
        return static_cast<std::int32_t>(uintn(s, p, 4)) / 65536.0;
    if (b == 30 && dict) {
        std::string v;
        for (;;) {
            auto c = byte(s, p);
            for (auto n : {c >> 4, c & 15}) {
                if (n == 15) {
                    check(!v.empty(), "Real CFF vazio");
                    return std::stod(v);
                }
                check(n != 13, "Real CFF inválido");
                v += n < 10    ? std::string(1, char('0' + n))
                     : n == 10 ? "."
                     : n == 11 ? "E"
                     : n == 12 ? "E-"
                               : "-";
            }
        }
    }
    throw std::runtime_error("Número CFF inválido");
}
using Dict = std::map<int, std::vector<double>>;
Dict dictionary(std::string_view s) {
    Dict d;
    std::vector<double> stack;
    std::size_t p = 0;
    while (p < s.size()) {
        int b = byte(s, p);
        if (b >= 28) {
            check(stack.size() < 48, "Pilha CFF excedida");
            stack.push_back(number(s, p, b, true));
        } else {
            if (b == 12)
                b = 1200 + byte(s, p);
            d[b] = std::move(stack);
            stack.clear();
        }
    }
    check(stack.empty(), "DICT CFF incompleto");
    return d;
}
int integer(double v) {
    check(std::isfinite(v) && v >= 0 && v <= 64000000 && std::floor(v) == v, "Offset CFF inválido");
    return int(v);
}
const std::vector<std::string> &strings() {
    static const auto names = [] {
        std::istringstream in(
            ".notdef space exclam quotedbl numbersign dollar percent ampersand quoteright "
            "parenleft parenright asterisk plus comma hyphen period slash zero one two three four "
            "five six seven eight nine colon semicolon less equal greater question at A B C D E F "
            "G H I J K L M N O P Q R S T U V W X Y Z bracketleft backslash bracketright "
            "asciicircum underscore quoteleft a b c d e f g h i j k l m n o p q r s t u v w x y z "
            "braceleft bar braceright asciitilde exclamdown cent sterling fraction yen florin "
            "section currency quotesingle quotedblleft guillemotleft guilsinglleft guilsinglright "
            "fi fl endash dagger daggerdbl periodcentered paragraph bullet quotesinglbase "
            "quotedblbase quotedblright guillemotright ellipsis perthousand questiondown grave "
            "acute circumflex tilde macron breve dotaccent dieresis ring cedilla hungarumlaut "
            "ogonek caron emdash AE ordfeminine Lslash Oslash OE ordmasculine ae dotlessi lslash "
            "oslash oe germandbls onesuperior logicalnot mu trademark Eth onehalf plusminus Thorn "
            "onequarter divide brokenbar degree thorn threequarters twosuperior registered minus "
            "eth multiply threesuperior copyright Aacute Acircumflex Adieresis Agrave Aring Atilde "
            "Ccedilla Eacute Ecircumflex Edieresis Egrave Iacute Icircumflex Idieresis Igrave "
            "Ntilde Oacute Ocircumflex Odieresis Ograve Otilde Scaron Uacute Ucircumflex Udieresis "
            "Ugrave Yacute Ydieresis Zcaron aacute acircumflex adieresis agrave aring atilde "
            "ccedilla eacute ecircumflex edieresis egrave iacute icircumflex idieresis igrave "
            "ntilde oacute ocircumflex odieresis ograve otilde scaron uacute ucircumflex udieresis "
            "ugrave yacute ydieresis zcaron");
        std::vector<std::string> n;
        std::string word;
        while (in >> word)
            n.push_back(word);
        return n;
    }();
    return names;
}
} // namespace
std::string CFF::standard_name(int code, bool win) {
    if (code >= 32 && code <= 126) {
        if (win && code == 39)
            return "quotesingle";
        if (win && code == 96)
            return "grave";
        return strings().at(code - 31);
    }
    static const std::map<int, std::string> standard = {{161, "exclamdown"},
                                                        {162, "cent"},
                                                        {163, "sterling"},
                                                        {164, "fraction"},
                                                        {165, "yen"},
                                                        {166, "florin"},
                                                        {167, "section"},
                                                        {168, "currency"},
                                                        {169, "quotesingle"},
                                                        {170, "quotedblleft"},
                                                        {171, "guillemotleft"},
                                                        {172, "guilsinglleft"},
                                                        {173, "guilsinglright"},
                                                        {174, "fi"},
                                                        {175, "fl"},
                                                        {177, "endash"},
                                                        {178, "dagger"},
                                                        {179, "daggerdbl"},
                                                        {180, "periodcentered"},
                                                        {182, "paragraph"},
                                                        {183, "bullet"},
                                                        {184, "quotesinglbase"},
                                                        {185, "quotedblbase"},
                                                        {186, "quotedblright"},
                                                        {187, "guillemotright"},
                                                        {188, "ellipsis"},
                                                        {189, "perthousand"},
                                                        {191, "questiondown"},
                                                        {193, "grave"},
                                                        {194, "acute"},
                                                        {195, "circumflex"},
                                                        {196, "tilde"},
                                                        {197, "macron"},
                                                        {198, "breve"},
                                                        {199, "dotaccent"},
                                                        {200, "dieresis"},
                                                        {202, "ring"},
                                                        {203, "cedilla"},
                                                        {205, "hungarumlaut"},
                                                        {206, "ogonek"},
                                                        {207, "caron"},
                                                        {208, "emdash"},
                                                        {225, "AE"},
                                                        {227, "ordfeminine"},
                                                        {232, "Lslash"},
                                                        {233, "Oslash"},
                                                        {234, "OE"},
                                                        {235, "ordmasculine"},
                                                        {241, "ae"},
                                                        {245, "dotlessi"},
                                                        {248, "lslash"},
                                                        {249, "oslash"},
                                                        {250, "oe"},
                                                        {251, "germandbls"}};
    static const std::map<int, std::string> ansi = {
        {128, "Euro"},         {130, "quotesinglbase"}, {131, "florin"},
        {132, "quotedblbase"}, {133, "ellipsis"},       {134, "dagger"},
        {135, "daggerdbl"},    {136, "circumflex"},     {137, "perthousand"},
        {138, "Scaron"},       {139, "guilsinglleft"},  {140, "OE"},
        {142, "Zcaron"},       {145, "quoteleft"},      {146, "quoteright"},
        {147, "quotedblleft"}, {148, "quotedblright"},  {149, "bullet"},
        {150, "endash"},       {151, "emdash"},         {152, "tilde"},
        {153, "trademark"},    {154, "scaron"},         {155, "guilsinglright"},
        {156, "oe"},           {158, "zcaron"},         {159, "Ydieresis"}};
    if (win && code >= 160 && code <= 255) {
        static const auto latin = [] {
            std::istringstream in(
                "space exclamdown cent sterling currency yen brokenbar section dieresis copyright "
                "ordfeminine guillemotleft logicalnot hyphen registered macron degree plusminus "
                "twosuperior threesuperior acute mu paragraph periodcentered cedilla onesuperior "
                "ordmasculine guillemotright onequarter onehalf threequarters questiondown Agrave "
                "Aacute Acircumflex Atilde Adieresis Aring AE Ccedilla Egrave Eacute Ecircumflex "
                "Edieresis Igrave Iacute Icircumflex Idieresis Eth Ntilde Ograve Oacute "
                "Ocircumflex Otilde Odieresis multiply Oslash Ugrave Uacute Ucircumflex Udieresis "
                "Yacute Thorn germandbls agrave aacute acircumflex atilde adieresis aring ae "
                "ccedilla egrave eacute ecircumflex edieresis igrave iacute icircumflex idieresis "
                "eth ntilde ograve oacute ocircumflex otilde odieresis divide oslash ugrave uacute "
                "ucircumflex udieresis yacute thorn ydieresis");
            std::vector<std::string> n;
            std::string w;
            while (in >> w)
                n.push_back(w);
            return n;
        }();
        return latin.at(code - 160);
    }
    const auto &table = win ? ansi : standard;
    auto it = table.find(code);
    return it == table.end() ? ".notdef" : it->second;
}
std::vector<std::string_view> CFF::index(std::size_t &p) const {
    auto s = view(bytes_);
    unsigned count = uintn(s, p, 2);
    if (!count)
        return {};
    int width = byte(s, p);
    check(width >= 1 && width <= 4, "INDEX CFF inválido");
    std::vector<unsigned> offsets;
    for (unsigned i = 0; i <= count; ++i)
        offsets.push_back(uintn(s, p, width));
    check(offsets.front() == 1, "INDEX CFF início inválido");
    std::vector<std::string_view> out;
    for (unsigned i = 0; i < count; ++i) {
        check(offsets[i + 1] >= offsets[i] && offsets[i + 1] - 1 <= s.size() - p,
              "INDEX CFF truncado");
        out.push_back(s.substr(p + offsets[i] - 1, offsets[i + 1] - offsets[i]));
    }
    p += offsets.back() - 1;
    return out;
}
CFF::CFF(Bytes bytes) : bytes_(std::move(bytes)) {
    auto s = view(bytes_);
    check(s.size() >= 4 && bytes_[0] == 1, "CFF1 esperado");
    std::size_t p = bytes_[2];
    check(p >= 4 && p <= s.size(), "Cabeçalho CFF inválido");
    index(p);
    auto tops = index(p);
    check(tops.size() == 1, "FontSet CFF não suportado");
    auto custom = index(p);
    global_ = index(p);
    auto d = dictionary(tops[0]);
    check(!d.count(1230), "CFF CID ainda não suportado");
    check(d[17].size() == 1, "CFF sem CharStrings");
    p = integer(d[17][0]);
    chars_ = index(p);
    check(!chars_.empty(), "CFF sem glifos");
    if (d.count(1206))
        check(d[1206].size() == 1 && d[1206][0] == 2, "CharString não Type2");
    if (d.count(1207)) {
        const auto &m = d[1207];
        check(m.size() == 6, "FontMatrix inválida");
        matrix_ = {m[0] * 1000, m[1] * 1000, m[2] * 1000, m[3] * 1000, m[4] * 1000, m[5] * 1000};
    }
    if (d.count(18)) {
        check(d[18].size() == 2, "Private CFF inválido");
        auto len = integer(d[18][0]), off = integer(d[18][1]);
        check(static_cast<std::size_t>(off) <= s.size() &&
                  static_cast<std::size_t>(len) <= s.size() - off,
              "Private CFF truncado");
        auto priv = dictionary(s.substr(off, len));
        if (priv.count(19)) {
            check(priv[19].size() == 1, "Subrs inválido");
            p = off + integer(priv[19][0]);
            local_ = index(p);
        }
    }
    auto name = [&](unsigned sid) {
        if (sid < strings().size())
            return strings()[sid];
        if (sid >= 391 && sid - 391 < custom.size())
            return std::string(custom[sid - 391]);
        return std::string("SID") + std::to_string(sid);
    };
    std::vector<unsigned> charset{0};
    p = d.count(15) ? integer(d[15].at(0)) : 0;
    if (p == 0) {
        check(chars_.size() <= 229, "Charset ISOAdobe excedido");
        for (unsigned i = 1; i < chars_.size(); ++i)
            charset.push_back(i);
    } else {
        check(p > 2, "Charset CFF predefinido não suportado");
        int format = byte(s, p);
        check(format <= 2, "Charset CFF inválido");
        while (charset.size() < chars_.size()) {
            unsigned sid = uintn(s, p, 2), count = format ? uintn(s, p, format) : 0;
            check(count < chars_.size() - charset.size(), "Charset CFF excedido");
            for (unsigned i = 0; i <= count; ++i)
                charset.push_back(sid + i);
        }
    }
    for (unsigned i = 0; i < charset.size(); ++i)
        names_[name(charset[i])] = i;
    p = d.count(16) ? integer(d[16].at(0)) : 0;
    if (p == 0) {
        for (int c = 0; c < 256; ++c)
            encoding_[c] = named(standard_name(c));
    } else {
        check(p > 1, "ExpertEncoding CFF não suportado");
        int format = byte(s, p), count = byte(s, p), gid = 1;
        check((format & 127) <= 1, "Encoding CFF inválido");
        for (int i = 0; i < count; ++i) {
            int c = byte(s, p), n = (format & 127) ? byte(s, p) : 0;
            check(c + n <= 255, "Encoding CFF excedido");
            for (int j = 0; j <= n; ++j) {
                check(gid < int(chars_.size()), "Encoding CFF glifo inválido");
                encoding_[c + j] = gid++;
            }
        }
        if (format & 128) {
            int n = byte(s, p);
            while (n--) {
                int c = byte(s, p);
                encoding_[c] = named(name(uintn(s, p, 2)));
            }
        }
    }
}
int CFF::named(const std::string &name) const {
    auto it = names_.find(name);
    return it == names_.end() ? 0 : it->second;
}
const Path &CFF::glyph(int id) {
    check(id >= 0 && static_cast<std::size_t>(id) < chars_.size(), "Glifo CFF inválido");
    auto found = cache_.find(id);
    if (found != cache_.end())
        return found->second;
    Path path;
    std::vector<double> a;
    Point current{0, 0};
    int hints = 0, budget = 100000;
    bool width = false, ended = false;
    auto close = [&] {
        if (!path.empty() && !path.back().empty())
            path.back().push_back(path.back().front());
    };
    auto move = [&](double x, double y) {
        close();
        current.x += x;
        current.y += y;
        path.push_back({current});
    };
    auto line = [&](double x, double y) {
        check(!path.empty(), "Linha CFF sem moveto");
        current.x += x;
        current.y += y;
        path.back().push_back(current);
    };
    auto curve = [&](double x1, double y1, double x2, double y2, double x3, double y3) {
        check(!path.empty(), "Curva CFF sem moveto");
        Point p = current, b{p.x + x1, p.y + y1}, c{b.x + x2, b.y + y2}, d{c.x + x3, c.y + y3};
        for (int i = 1; i <= 16; ++i) {
            double t = i / 16.0, u = 1 - t;
            path.back().push_back(
                {u * u * u * p.x + 3 * u * u * t * b.x + 3 * u * t * t * c.x + t * t * t * d.x,
                 u * u * u * p.y + 3 * u * u * t * b.y + 3 * u * t * t * c.y + t * t * t * d.y});
        }
        current = d;
    };
    std::function<void(std::string_view, int)> run = [&](std::string_view code, int depth) {
        check(depth < 32, "Subrotina CFF circular");
        std::size_t p = 0;
        while (p < code.size() && !ended) {
            check(--budget > 0, "Programa CFF excedido");
            int op = byte(code, p);
            if (op >= 32 || op == 28) {
                check(a.size() < 48, "Pilha Type2 excedida");
                a.push_back(number(code, p, op, false));
                continue;
            }
            if (op == 12)
                op = 1200 + byte(code, p);
            if (op == 10 || op == 29) {
                check(!a.empty(), "Subr sem operando");
                auto &sub = op == 10 ? local_ : global_;
                double n = a.back();
                a.pop_back();
                int bias = sub.size() < 1240 ? 107 : sub.size() < 33900 ? 1131 : 32768;
                check(std::isfinite(n) && n >= -32768 && n <= 65535 && std::floor(n) == n,
                      "Subr inválido");
                int index = int(n) + bias;
                check(index >= 0 && static_cast<std::size_t>(index) < sub.size(),
                      "Subr fora da fonte");
                run(sub[index], depth + 1);
                continue;
            }
            if (op == 11) {
                check(depth > 0, "Return CFF inválido");
                return;
            }
            if (op == 1 || op == 3 || op == 18 || op == 23 || op == 19 || op == 20) {
                if (!width && a.size() % 2)
                    a.erase(a.begin());
                width = true;
                check(a.size() % 2 == 0, "Hints CFF inválidos");
                hints += a.size() / 2;
                a.clear();
                check(hints <= 96, "Hints CFF excedidos");
                if (op == 19 || op == 20) {
                    p += (hints + 7) / 8;
                    check(p <= code.size(), "Hintmask truncada");
                }
                continue;
            }
            if (op == 4 || op == 21 || op == 22) {
                std::size_t n = op == 21 ? 2 : 1;
                if (!width && a.size() == n + 1)
                    a.erase(a.begin());
                width = true;
                check(a.size() == n, "Moveto CFF inválido");
                move(op == 4 ? 0 : a[0], op == 22 ? 0 : a.back());
            } else if (op == 5) {
                check(a.size() >= 2 && a.size() % 2 == 0, "Rlineto inválido");
                for (std::size_t i = 0; i < a.size(); i += 2)
                    line(a[i], a[i + 1]);
            } else if (op == 6 || op == 7) {
                check(!a.empty(), "Lineto vazio");
                bool h = op == 6;
                for (double v : a) {
                    line(h ? v : 0, h ? 0 : v);
                    h = !h;
                }
            } else if (op == 8) {
                check(!a.empty() && a.size() % 6 == 0, "Rrcurveto inválido");
                for (std::size_t i = 0; i < a.size(); i += 6)
                    curve(a[i], a[i + 1], a[i + 2], a[i + 3], a[i + 4], a[i + 5]);
            } else if (op == 24) {
                check(a.size() >= 8 && (a.size() - 2) % 6 == 0, "Rcurveline inválido");
                std::size_t i = 0;
                for (; i + 2 < a.size(); i += 6)
                    curve(a[i], a[i + 1], a[i + 2], a[i + 3], a[i + 4], a[i + 5]);
                line(a[i], a[i + 1]);
            } else if (op == 25) {
                check(a.size() >= 8 && (a.size() - 6) % 2 == 0, "Rlinecurve inválido");
                std::size_t i = 0;
                for (; i + 6 < a.size(); i += 2)
                    line(a[i], a[i + 1]);
                curve(a[i], a[i + 1], a[i + 2], a[i + 3], a[i + 4], a[i + 5]);
            } else if (op == 26 || op == 27) {
                check(a.size() >= 4 && (a.size() % 4 == 0 || a.size() % 4 == 1),
                      "Curveto inválido");
                std::size_t i = a.size() % 4;
                double extra = i ? a[0] : 0;
                for (; i < a.size(); i += 4) {
                    if (op == 26)
                        curve(extra, a[i], a[i + 1], a[i + 2], 0, a[i + 3]);
                    else
                        curve(a[i], extra, a[i + 1], a[i + 2], a[i + 3], 0);
                    extra = 0;
                }
            } else if (op == 30 || op == 31) {
                check(a.size() >= 4 && (a.size() % 4 == 0 || a.size() % 4 == 1),
                      "Hvcurveto inválido");
                bool h = op == 31;
                for (std::size_t i = 0; i + 3 < a.size(); i += 4) {
                    double last = a.size() - i == 5 ? a[i + 4] : 0;
                    if (h)
                        curve(a[i], 0, a[i + 1], a[i + 2], last, a[i + 3]);
                    else
                        curve(0, a[i], a[i + 1], a[i + 2], a[i + 3], last);
                    h = !h;
                }
            } else if (op == 1234) {
                check(a.size() == 7, "Hflex inválido");
                curve(a[0], 0, a[1], a[2], a[3], 0);
                curve(a[4], 0, a[5], -a[2], a[6], 0);
            } else if (op == 1235) {
                check(a.size() == 13, "Flex inválido");
                curve(a[0], a[1], a[2], a[3], a[4], a[5]);
                curve(a[6], a[7], a[8], a[9], a[10], a[11]);
            } else if (op == 1236) {
                check(a.size() == 9, "Hflex1 inválido");
                curve(a[0], a[1], a[2], a[3], a[4], 0);
                curve(a[5], 0, a[6], a[7], a[8], -a[1] - a[3] - a[7]);
            } else if (op == 1237) {
                check(a.size() == 11, "Flex1 inválido");
                double x = a[0] + a[2] + a[4] + a[6] + a[8], y = a[1] + a[3] + a[5] + a[7] + a[9];
                curve(a[0], a[1], a[2], a[3], a[4], a[5]);
                curve(a[6], a[7], a[8], a[9], std::abs(x) > std::abs(y) ? a[10] : -x,
                      std::abs(x) > std::abs(y) ? -y : a[10]);
            } else if (op == 14) {
                if (!width && a.size() == 1)
                    a.clear();
                check(a.empty(), "Seac CFF ainda não suportado");
                close();
                ended = true;
            } else
                throw std::runtime_error("Operador Type2 não suportado: " + std::to_string(op));
            a.clear();
        }
    };
    run(chars_[id], 0);
    check(ended, "CharString sem endchar");
    for (auto &c : path)
        for (auto &p : c)
            p = matrix_(p);
    return cache_.emplace(id, std::move(path)).first->second;
}
} // namespace pdf
