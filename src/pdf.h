#pragma once
#include "common.h"
#include <map>
#include <set>

namespace pdf {
struct Value {
    enum Kind { Null, Number, Name, String, Array, Dictionary, Reference, Word } kind = Null;
    double number = 0;
    std::string text;
    std::vector<Value> array;
    std::map<std::string, Value> dictionary;
    int id = 0, generation = 0;
    const Value &operator[](const std::string &key) const;
    bool is(const std::string &name) const {
        return kind == Name && text == name;
    }
    int integer() const;
};
class Lexer {
    std::string_view source_;

  public:
    std::size_t position = 0;
    explicit Lexer(std::string_view source, std::size_t start = 0)
        : source_(source), position(start) {}
    void whitespace();
    bool done();
    std::string token();
    Value value(int depth = 0);
};
struct Object {
    Value value;
    std::string_view stream;
};
struct Page {
    Value resources, contents;
    double x = 0, y = 0, width = 612, height = 792;
};
class Document {
    Bytes bytes_;
    struct Entry {
        std::size_t offset;
        int generation;
        bool used;
        int container = 0, index = 0;
    };
    std::map<int, Entry> xref_;
    std::map<int, Object> objects_;
    std::vector<Page> pages_;
    int load_depth_ = 0;
    Object read_object(std::size_t offset, int id, int generation);
    Bytes decode(const Object &object);
    void walk(const Value &node, Value resources, Value box, std::set<int> &visited, int depth);

  public:
    explicit Document(const std::string &path);
    const Object &object(const Value &reference);
    const Value &resolve(const Value &value);
    Bytes stream(const Value &reference);
    std::string content(const Value &value, int depth = 0);
    const std::vector<Page> &pages() const {
        return pages_;
    }
};
} // namespace pdf
