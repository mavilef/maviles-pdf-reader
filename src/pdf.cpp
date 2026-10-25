#include "pdf.h"
#include "inflate.h"
#include <cctype>
#include <limits>

namespace pdf {
namespace {
bool space(char c) {
    return c == 0 || c == 9 || c == 10 || c == 12 || c == 13 || c == 32;
}
bool delimiter(char c) {
    return space(c) || std::string_view("()<>[]{}/% ").find(c) != std::string_view::npos;
}
int hex(char c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    throw std::runtime_error("Dígito hexadecimal inválido no PDF");
}
bool numeric(const std::string &s) {
    return !s.empty() && (std::isdigit(static_cast<unsigned char>(s[0])) || s[0] == '-' ||
                          s[0] == '+' || s[0] == '.');
}
} // namespace
const Value &Value::operator[](const std::string &key) const {
    static const Value empty;
    auto it = dictionary.find(key);
    return it == dictionary.end() ? empty : it->second;
}
int Value::integer() const {
    check(kind == Number && std::isfinite(number) && number >= std::numeric_limits<int>::min() &&
              number <= std::numeric_limits<int>::max() && std::floor(number) == number,
          "Inteiro PDF inválido");
    return static_cast<int>(number);
}
void Lexer::whitespace() {
    while (position < source_.size()) {
        if (space(source_[position])) {
            ++position;
            continue;
        }
        if (source_[position] != '%')
            break;
        while (position < source_.size() && source_[position] != '\r' && source_[position] != '\n')
            ++position;
    }
}
bool Lexer::done() {
    whitespace();
    return position == source_.size();
}
std::string Lexer::token() {
    whitespace();
    check(position < source_.size(), "PDF truncado");
    const auto start = position++;
    const char first = source_[start];
    if (first == '<' || first == '>') {
        if (position < source_.size() && source_[position] == first)
            ++position;
    } else if (first == '/' || !delimiter(first)) {
        while (position < source_.size() && !delimiter(source_[position]))
            ++position;
    }
    return std::string(source_.substr(start, position - start));
}
Value Lexer::value(int depth) {
    check(depth < 64, "Estrutura PDF profunda demais");
    const auto t = token();
    Value v;
    if (t == "[") {
        v.kind = Value::Array;
        for (;;) {
            whitespace();
            check(position < source_.size(), "Array PDF truncado");
            if (source_[position] == ']') {
                ++position;
                break;
            }
            check(v.array.size() < 1000000, "Array PDF grande demais");
            v.array.push_back(value(depth + 1));
        }
    } else if (t == "<<") {
        v.kind = Value::Dictionary;
        for (;;) {
            const auto key = value(depth + 1);
            if (key.kind == Value::Word && key.text == ">>")
                break;
            check(key.kind == Value::Name, "Chave de dicionário PDF inválida");
            v.dictionary[key.text] = value(depth + 1);
        }
    } else if (t == "(") {
        v.kind = Value::String;
        int nesting = 1;
        while (nesting) {
            check(position < source_.size(), "String PDF truncada");
            char c = source_[position++];
            if (c == '\\') {
                check(position < source_.size(), "Escape PDF truncado");
                c = source_[position++];
                if (c == '\r' || c == '\n') {
                    if (c == '\r' && position < source_.size() && source_[position] == '\n')
                        ++position;
                    continue;
                }
                if (c >= '0' && c <= '7') {
                    int n = c - '0';
                    for (int i = 0; i < 2 && position < source_.size() &&
                                    source_[position] >= '0' && source_[position] <= '7';
                         ++i)
                        n = n * 8 + source_[position++] - '0';
                    c = static_cast<char>(n);
                } else {
                    const std::string from = "nrtbf", to = "\n\r\t\b\f";
                    const auto p = from.find(c);
                    if (p != std::string::npos)
                        c = to[p];
                }
                v.text += c;
                continue;
            }
            if (c == '(')
                ++nesting;
            if (c == ')')
                --nesting;
            if (nesting)
                v.text += c;
        }
    } else if (t == "<") {
        v.kind = Value::String;
        int high = -1;
        for (;;) {
            check(position < source_.size(), "String hexadecimal truncada");
            const char c = source_[position++];
            if (c == '>')
                break;
            if (space(c))
                continue;
            const int n = hex(c);
            if (high < 0)
                high = n;
            else {
                v.text += static_cast<char>(high * 16 + n);
                high = -1;
            }
        }
        if (high >= 0)
            v.text += static_cast<char>(high * 16);
    } else if (t[0] == '/') {
        v.kind = Value::Name;
        for (std::size_t i = 1; i < t.size(); ++i) {
            if (t[i] == '#') {
                check(i + 2 < t.size(), "Nome PDF inválido");
                v.text += static_cast<char>(hex(t[i + 1]) * 16 + hex(t[i + 2]));
                i += 2;
            } else
                v.text += t[i];
        }
    } else if (numeric(t)) {
        v.kind = Value::Number;
        std::size_t used = 0;
        v.number = std::stod(t, &used);
        check(used == t.size() && std::isfinite(v.number), "Número PDF inválido");
        const auto saved = position;
        // Referências são sempre dois inteiros positivos seguidos de R.
        if (v.number >= 0 && std::floor(v.number) == v.number && !done()) {
            const auto second = token();
            if (!second.empty() && second.find_first_not_of("0123456789") == std::string::npos &&
                !done() && token() == "R") {
                v.id = v.integer();
                const auto generation = std::stoul(second);
                check(generation <= 65535, "Geração PDF inválida");
                v.generation = static_cast<int>(generation);
                v.kind = Value::Reference;
                return v;
            }
        }
        position = saved;
    } else if (t != "null") {
        v.kind = Value::Word;
        v.text = t;
    }
    return v;
}

Document::Document(const std::string &path) : bytes_(read_file(path)) {
    const auto source = view(bytes_);
    check(source.substr(0, 5) == "%PDF-", "O arquivo não é um PDF");
    auto start = source.rfind("startxref");
    check(start != std::string_view::npos, "PDF sem startxref");
    Lexer tail(source, start + 9);
    std::size_t offset = std::stoull(tail.token());
    Value root;
    std::set<std::size_t> visited;
    for (;;) {
        check(offset < source.size() && visited.insert(offset).second && visited.size() <= 128,
              "Cadeia xref inválida");
        Lexer x(source, offset);
        Value trailer;
        if (x.token() == "xref") {
            for (;;) {
                const auto first = x.token();
                if (first == "trailer")
                    break;
                const int begin = std::stoi(first), count = std::stoi(x.token());
                check(begin >= 0 && count >= 0 && count <= 1000000 && begin <= 1000000 - count,
                      "Tabela xref grande/inválida");
                for (int i = 0; i < count; ++i) {
                    const auto where = std::stoull(x.token());
                    const int generation = std::stoi(x.token());
                    const auto flag = x.token();
                    check(flag == "n" || flag == "f", "Entrada xref inválida");
                    xref_.emplace(begin + i, Entry{where, generation, flag == "n"});
                }
            }
            trailer = x.value();
        } else {
            Lexer header(source, offset);
            const int id = header.value().integer(), generation = header.value().integer();
            const auto obj = read_object(offset, id, generation);
            trailer = obj.value;
            check(trailer["Type"].is("XRef"), "Objeto não é xref stream");
            const auto &w = trailer["W"].array;
            check(w.size() == 3, "W de xref inválido");
            int widths[3];
            for (int i = 0; i < 3; ++i) {
                widths[i] = w[i].integer();
                check(widths[i] >= 0 && widths[i] <= 4, "Largura xref inválida");
            }
            const int size = trailer["Size"].integer();
            check(size > 0 && size <= 1000000, "Size xref inválido");
            auto ranges = trailer["Index"].array;
            if (trailer["Index"].kind == Value::Null) {
                Value a, b;
                a.kind = b.kind = Value::Number;
                b.number = size;
                ranges = {a, b};
            }
            check(ranges.size() % 2 == 0, "Index xref inválido");
            const auto data = decode(obj);
            std::size_t pos = 0;
            for (std::size_t r = 0; r < ranges.size(); r += 2) {
                const int begin = ranges[r].integer(), count = ranges[r + 1].integer();
                check(begin >= 0 && count >= 0 && begin <= size && count <= size - begin,
                      "Intervalo xref inválido");
                for (int i = 0; i < count; ++i) {
                    std::uint64_t fields[3] = {1, 0, 0};
                    for (int k = 0; k < 3; ++k) {
                        if (widths[k])
                            fields[k] = 0;
                        for (int n = 0; n < widths[k]; ++n) {
                            check(pos < data.size(), "Xref stream truncado");
                            fields[k] = fields[k] * 256 + data[pos++];
                        }
                    }
                    check(fields[0] <= 2 && fields[2] <= 1000000, "Entrada xref inválida");
                    Entry e{static_cast<std::size_t>(fields[1]), static_cast<int>(fields[2]),
                            fields[0] != 0};
                    if (fields[0] == 2) {
                        check(fields[1] > 0 && fields[1] < 1000000, "Object stream inválido");
                        e.container = static_cast<int>(fields[1]);
                        e.index = static_cast<int>(fields[2]);
                        e.generation = 0;
                    }
                    xref_.emplace(begin + i, e);
                }
            }
            check(pos == data.size(), "Comprimento xref stream inválido");
        }
        check(trailer["Encrypt"].kind == Value::Null, "PDF com criptografia ainda não suportado");
        check(trailer["XRefStm"].kind == Value::Null, "PDF com xref híbrido ainda não suportado");
        if (root.kind == Value::Null)
            root = trailer["Root"];
        if (trailer["Prev"].kind == Value::Null)
            break;
        const int previous = trailer["Prev"].integer();
        check(previous >= 0, "Offset xref inválido");
        offset = previous;
    }
    check(root.kind == Value::Reference, "Catálogo PDF ausente");
    std::set<int> nodes;
    walk(resolve(root)["Pages"], {}, {}, nodes, 0);
    check(!pages_.empty(), "PDF não contém páginas");
}
const Object &Document::object(const Value &ref) {
    check(ref.kind == Value::Reference, "Objeto PDF não é referência");
    auto entry = xref_.find(ref.id);
    check(entry != xref_.end() && entry->second.used && entry->second.generation == ref.generation,
          "Referência PDF inexistente: " + std::to_string(ref.id));
    auto cached = objects_.find(ref.id);
    if (cached != objects_.end())
        return cached->second;
    check(load_depth_ < 32, "Referência circular em stream PDF");
    struct Guard {
        int &depth;
        explicit Guard(int &d) : depth(d) {
            ++depth;
        }
        ~Guard() {
            --depth;
        }
    } guard(load_depth_);
    if (entry->second.container) {
        Value container;
        container.kind = Value::Reference;
        container.id = entry->second.container;
        const auto &obj = object(container);
        check(obj.value["Type"].is("ObjStm"), "Objeto não é ObjStm");
        const int count = resolve(obj.value["N"]).integer();
        const int first = resolve(obj.value["First"]).integer();
        check(count > 0 && count <= 1000000 && entry->second.index < count,
              "Índice ObjStm inválido");
        const auto data = stream(container);
        check(first >= 0 && static_cast<std::size_t>(first) <= data.size(),
              "First ObjStm inválido");
        Lexer header(view(data).substr(0, first));
        std::vector<std::pair<int, int>> items;
        for (int i = 0; i < count; ++i) {
            const int id = header.value().integer(), offset = header.value().integer();
            check(id > 0 && offset >= 0 && static_cast<std::size_t>(offset) < data.size() - first &&
                      (items.empty() || offset > items.back().second),
                  "Cabeçalho ObjStm inválido");
            items.emplace_back(id, offset);
        }
        check(header.done(), "Cabeçalho ObjStm excedido");
        const auto selected = items[entry->second.index];
        check(selected.first == ref.id, "Objeto ObjStm divergente");
        const auto end = entry->second.index + 1 < count
                             ? first + items[entry->second.index + 1].second
                             : data.size();
        Lexer value(view(data).substr(first + selected.second, end - first - selected.second));
        Object result;
        result.value = value.value();
        check(value.done(), "Objeto compactado inválido");
        return objects_.emplace(ref.id, std::move(result)).first->second;
    }
    return objects_.emplace(ref.id, read_object(entry->second.offset, ref.id, ref.generation))
        .first->second;
}
Object Document::read_object(std::size_t offset, int id, int generation) {
    Lexer lexer(view(bytes_), offset);
    check(std::stoi(lexer.token()) == id && std::stoi(lexer.token()) == generation &&
              lexer.token() == "obj",
          "Offset xref não aponta para o objeto esperado");
    Object object;
    object.value = lexer.value();
    const auto next = lexer.token();
    if (next == "stream") {
        auto pos = lexer.position;
        check(pos < bytes_.size() && (bytes_[pos] == '\r' || bytes_[pos] == '\n'),
              "Stream sem quebra de linha");
        if (bytes_[pos++] == '\r' && pos < bytes_.size() && bytes_[pos] == '\n')
            ++pos;
        const int length = resolve(object.value["Length"]).integer();
        check(length >= 0 && static_cast<std::size_t>(length) <= bytes_.size() - pos,
              "Comprimento de stream inválido");
        object.stream = view(bytes_).substr(pos, length);
        lexer.position = pos + length;
        check(lexer.token() == "endstream" && lexer.token() == "endobj",
              "Stream PDF não terminado");
    } else
        check(next == "endobj", "Objeto PDF não terminado");
    return object;
}
const Value &Document::resolve(const Value &value) {
    const Value *v = &value;
    for (int i = 0; i < 32; ++i) {
        if (v->kind != Value::Reference)
            return *v;
        v = &object(*v).value;
    }
    throw std::runtime_error("Referência PDF circular");
}
Bytes Document::stream(const Value &reference) {
    return decode(object(reference));
}
Bytes Document::decode(const Object &o) {
    Bytes bytes(o.stream.begin(), o.stream.end());
    const auto &filter = resolve(o.value["Filter"]);
    std::vector<Value> filters =
        filter.kind == Value::Array ? filter.array : std::vector<Value>{filter};
    for (const auto &f : filters) {
        if (f.kind == Value::Null)
            continue;
        if (f.is("FlateDecode"))
            bytes = inflate(view(bytes));
        else if (!f.is("DCTDecode"))
            throw std::runtime_error("Filtro PDF não suportado: " + f.text);
    }
    const auto &params = resolve(o.value["DecodeParms"]);
    const int predictor =
        params["Predictor"].kind == Value::Null ? 1 : params["Predictor"].integer();
    if (predictor != 1) {
        check(predictor >= 10 && predictor <= 15, "Predictor não suportado");
        const int columns = params["Columns"].kind == Value::Null ? 1 : params["Columns"].integer();
        const int colors = params["Colors"].kind == Value::Null ? 1 : params["Colors"].integer();
        const int bits = params["BitsPerComponent"].kind == Value::Null
                             ? 8
                             : params["BitsPerComponent"].integer();
        check(columns > 0 && columns <= 1000000 && colors > 0 && colors <= 4 && bits == 8,
              "Parâmetros PNG inválidos/não suportados");
        const std::size_t row = static_cast<std::size_t>(columns) * colors;
        check(bytes.size() % (row + 1) == 0, "Predictor PNG truncado");
        Bytes decoded;
        for (std::size_t p = 0; p < bytes.size(); p += row + 1) {
            const int filter = bytes[p];
            check(filter <= 4, "Filtro PNG inválido");
            const auto base = decoded.size();
            for (std::size_t j = 0; j < row; ++j) {
                int a = j >= static_cast<std::size_t>(colors) ? decoded[base + j - colors] : 0;
                int b = base >= row ? decoded[base + j - row] : 0;
                int c = base >= row && j >= static_cast<std::size_t>(colors)
                            ? decoded[base + j - row - colors]
                            : 0;
                int predict = 0;
                if (filter == 1)
                    predict = a;
                if (filter == 2)
                    predict = b;
                if (filter == 3)
                    predict = (a + b) / 2;
                if (filter == 4) {
                    int v = a + b - c, pa = std::abs(v - a), pb = std::abs(v - b),
                        pc = std::abs(v - c);
                    predict = pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
                }
                decoded.push_back(static_cast<std::uint8_t>(bytes[p + 1 + j] + predict));
            }
        }
        bytes = std::move(decoded);
    }
    return bytes;
}
std::string Document::content(const Value &value, int depth) {
    check(depth < 32, "Conteúdo PDF circular");
    const auto &v = resolve(value);
    if (v.kind == Value::Array) {
        std::string joined;
        for (const auto &item : v.array) {
            joined += content(item, depth + 1);
            joined += '\n';
            check(joined.size() < 64 * 1024 * 1024, "Conteúdo de página grande demais");
        }
        return joined;
    }
    if (v.kind == Value::Null)
        return {};
    return std::string(view(stream(value)));
}
void Document::walk(const Value &node, Value resources, Value box, std::set<int> &visited,
                    int depth) {
    check(depth < 64 && pages_.size() < 100000, "Árvore de páginas excedida");
    if (node.kind == Value::Reference)
        check(visited.insert(node.id).second, "Árvore de páginas circular");
    const auto &v = resolve(node);
    if (v["Resources"].kind != Value::Null)
        resources = v["Resources"];
    if (v["MediaBox"].kind != Value::Null)
        box = v["MediaBox"];
    check(v["Rotate"].kind == Value::Null || v["Rotate"].integer() % 360 == 0,
          "Rotação de página ainda não suportada");
    if (v["Type"].is("Pages")) {
        const auto &kids = resolve(v["Kids"]);
        check(kids.kind == Value::Array, "Árvore de páginas sem Kids");
        for (const auto &child : kids.array)
            walk(child, resources, box, visited, depth + 1);
    } else {
        check(v["Type"].is("Page"), "Nó de página inválido");
        const auto &b = resolve(box);
        check(b.kind == Value::Array && b.array.size() == 4, "MediaBox inválido");
        Page p;
        p.resources = resources;
        p.contents = v["Contents"];
        p.x = b.array[0].number;
        p.y = b.array[1].number;
        p.width = b.array[2].number - p.x;
        p.height = b.array[3].number - p.y;
        check(p.width > 0 && p.height > 0 && p.width < 100000 && p.height < 100000,
              "Dimensões de página inválidas");
        pages_.push_back(std::move(p));
    }
}
} // namespace pdf
