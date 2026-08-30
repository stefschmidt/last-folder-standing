// Minimal JSON reader/writer.
//
// Design constraints (see docs/DEVELOPMENT.md rule 1): this header is also used inside the
// shell extension, which is loaded into explorer.exe and every file-dialog host.
// Therefore:
//   * no exceptions are thrown by the parser -- failures return std::nullopt
//   * recursion is depth-limited so hostile/corrupt input cannot blow the stack
//   * no dependencies beyond the standard library
//
// Values are stored as UTF-16 (std::wstring); Parse() takes UTF-16, Serialize()
// returns UTF-16. Convert at the file boundary with lfs::Utf8ToWide/WideToUtf8.
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace lfs::json {

enum class Type { Null, Bool, Number, String, Array, Object };

class Value;
using ValuePtr = std::shared_ptr<Value>;

// Children are held through shared_ptr so Value can contain itself without
// relying on incomplete-type container support.
class Value {
public:
    Value() = default;
    explicit Value(bool b) : type_(Type::Bool), bool_(b) {}
    explicit Value(double n) : type_(Type::Number), number_(n) {}
    explicit Value(int n) : type_(Type::Number), number_(static_cast<double>(n)) {}
    explicit Value(std::wstring s) : type_(Type::String), string_(std::move(s)) {}
    explicit Value(const wchar_t* s) : type_(Type::String), string_(s ? s : L"") {}

    static Value MakeArray() {
        Value v;
        v.type_ = Type::Array;
        return v;
    }
    static Value MakeObject() {
        Value v;
        v.type_ = Type::Object;
        return v;
    }

    Type type() const { return type_; }
    bool IsNull() const { return type_ == Type::Null; }
    bool IsArray() const { return type_ == Type::Array; }
    bool IsObject() const { return type_ == Type::Object; }
    bool IsString() const { return type_ == Type::String; }
    bool IsNumber() const { return type_ == Type::Number; }

    // Accessors never fail; they fall back to the supplied default.
    bool AsBool(bool fallback = false) const { return type_ == Type::Bool ? bool_ : fallback; }
    double AsNumber(double fallback = 0.0) const {
        return type_ == Type::Number ? number_ : fallback;
    }
    int AsInt(int fallback = 0) const {
        return type_ == Type::Number ? static_cast<int>(number_) : fallback;
    }
    const std::wstring& AsString(const std::wstring& fallback) const {
        return type_ == Type::String ? string_ : fallback;
    }
    std::wstring AsString() const { return type_ == Type::String ? string_ : std::wstring{}; }

    // Array access.
    size_t size() const {
        if (type_ == Type::Array) return items_.size();
        if (type_ == Type::Object) return members_.size();
        return 0;
    }
    const Value* At(size_t i) const {
        if (type_ != Type::Array || i >= items_.size()) return nullptr;
        return items_[i].get();
    }
    void Push(Value v) {
        if (type_ != Type::Array) return;
        items_.push_back(std::make_shared<Value>(std::move(v)));
    }

    // Object access. Keys are compared exactly (JSON is case-sensitive).
    const Value* Find(std::wstring_view key) const {
        if (type_ != Type::Object) return nullptr;
        for (const auto& [k, v] : members_) {
            if (k == key) return v.get();
        }
        return nullptr;
    }
    void Set(std::wstring key, Value v) {
        if (type_ != Type::Object) return;
        members_.emplace_back(std::move(key), std::make_shared<Value>(std::move(v)));
    }
    const std::vector<std::pair<std::wstring, ValuePtr>>& members() const { return members_; }

private:
    Type type_ = Type::Null;
    bool bool_ = false;
    double number_ = 0.0;
    std::wstring string_;
    std::vector<ValuePtr> items_;
    std::vector<std::pair<std::wstring, ValuePtr>> members_;

    friend class Parser;
};

namespace detail {

constexpr int kMaxDepth = 64;

class Parser {
public:
    explicit Parser(std::wstring_view text) : text_(text) {}

    std::optional<Value> ParseDocument() {
        SkipWs();
        auto v = ParseValue(0);
        if (!v) return std::nullopt;
        SkipWs();
        if (pos_ != text_.size()) return std::nullopt;  // trailing garbage
        return v;
    }

private:
    std::wstring_view text_;
    size_t pos_ = 0;

    bool Eof() const { return pos_ >= text_.size(); }
    wchar_t Peek() const { return pos_ < text_.size() ? text_[pos_] : L'\0'; }

    void SkipWs() {
        while (!Eof()) {
            const wchar_t c = text_[pos_];
            if (c == L' ' || c == L'\t' || c == L'\r' || c == L'\n' || c == 0xFEFF) {
                ++pos_;
            } else {
                break;
            }
        }
    }

    bool Literal(std::wstring_view lit) {
        if (text_.size() - pos_ < lit.size()) return false;
        if (text_.compare(pos_, lit.size(), lit) != 0) return false;
        pos_ += lit.size();
        return true;
    }

    std::optional<Value> ParseValue(int depth) {
        if (depth > kMaxDepth) return std::nullopt;
        SkipWs();
        if (Eof()) return std::nullopt;
        switch (Peek()) {
            case L'{': return ParseObject(depth);
            case L'[': return ParseArray(depth);
            case L'"': {
                auto s = ParseString();
                if (!s) return std::nullopt;
                return Value(std::move(*s));
            }
            case L't':
                if (!Literal(L"true")) return std::nullopt;
                return Value(true);
            case L'f':
                if (!Literal(L"false")) return std::nullopt;
                return Value(false);
            case L'n':
                if (!Literal(L"null")) return std::nullopt;
                return Value();
            default: return ParseNumber();
        }
    }

    std::optional<Value> ParseObject(int depth) {
        ++pos_;  // '{'
        Value obj = Value::MakeObject();
        SkipWs();
        if (Peek() == L'}') {
            ++pos_;
            return obj;
        }
        for (;;) {
            SkipWs();
            if (Peek() != L'"') return std::nullopt;
            auto key = ParseString();
            if (!key) return std::nullopt;
            SkipWs();
            if (Peek() != L':') return std::nullopt;
            ++pos_;
            auto val = ParseValue(depth + 1);
            if (!val) return std::nullopt;
            obj.Set(std::move(*key), std::move(*val));
            SkipWs();
            if (Peek() == L',') {
                ++pos_;
                continue;
            }
            if (Peek() == L'}') {
                ++pos_;
                return obj;
            }
            return std::nullopt;
        }
    }

    std::optional<Value> ParseArray(int depth) {
        ++pos_;  // '['
        Value arr = Value::MakeArray();
        SkipWs();
        if (Peek() == L']') {
            ++pos_;
            return arr;
        }
        for (;;) {
            auto val = ParseValue(depth + 1);
            if (!val) return std::nullopt;
            arr.Push(std::move(*val));
            SkipWs();
            if (Peek() == L',') {
                ++pos_;
                continue;
            }
            if (Peek() == L']') {
                ++pos_;
                return arr;
            }
            return std::nullopt;
        }
    }

    std::optional<std::wstring> ParseString() {
        if (Peek() != L'"') return std::nullopt;
        ++pos_;
        std::wstring out;
        while (!Eof()) {
            const wchar_t c = text_[pos_++];
            if (c == L'"') return out;
            if (c != L'\\') {
                if (c < 0x20) return std::nullopt;  // raw control char
                out.push_back(c);
                continue;
            }
            if (Eof()) return std::nullopt;
            const wchar_t esc = text_[pos_++];
            switch (esc) {
                case L'"': out.push_back(L'"'); break;
                case L'\\': out.push_back(L'\\'); break;
                case L'/': out.push_back(L'/'); break;
                case L'b': out.push_back(L'\b'); break;
                case L'f': out.push_back(L'\f'); break;
                case L'n': out.push_back(L'\n'); break;
                case L'r': out.push_back(L'\r'); break;
                case L't': out.push_back(L'\t'); break;
                case L'u': {
                    if (text_.size() - pos_ < 4) return std::nullopt;
                    unsigned code = 0;
                    for (int i = 0; i < 4; ++i) {
                        const wchar_t h = text_[pos_++];
                        code <<= 4;
                        if (h >= L'0' && h <= L'9') {
                            code |= static_cast<unsigned>(h - L'0');
                        } else if (h >= L'a' && h <= L'f') {
                            code |= static_cast<unsigned>(h - L'a' + 10);
                        } else if (h >= L'A' && h <= L'F') {
                            code |= static_cast<unsigned>(h - L'A' + 10);
                        } else {
                            return std::nullopt;
                        }
                    }
                    // Surrogate halves are kept verbatim; UTF-16 storage means a
                    // valid pair reassembles by itself.
                    out.push_back(static_cast<wchar_t>(code));
                    break;
                }
                default: return std::nullopt;
            }
        }
        return std::nullopt;  // unterminated
    }

    std::optional<Value> ParseNumber() {
        const size_t start = pos_;
        if (Peek() == L'-') ++pos_;
        bool digits = false;
        while (!Eof() && text_[pos_] >= L'0' && text_[pos_] <= L'9') {
            ++pos_;
            digits = true;
        }
        if (!Eof() && text_[pos_] == L'.') {
            ++pos_;
            while (!Eof() && text_[pos_] >= L'0' && text_[pos_] <= L'9') {
                ++pos_;
                digits = true;
            }
        }
        if (!digits) return std::nullopt;
        if (!Eof() && (text_[pos_] == L'e' || text_[pos_] == L'E')) {
            ++pos_;
            if (!Eof() && (text_[pos_] == L'+' || text_[pos_] == L'-')) ++pos_;
            bool expDigits = false;
            while (!Eof() && text_[pos_] >= L'0' && text_[pos_] <= L'9') {
                ++pos_;
                expDigits = true;
            }
            if (!expDigits) return std::nullopt;
        }
        const std::wstring token(text_.substr(start, pos_ - start));
        wchar_t* end = nullptr;
        const double d = ::wcstod(token.c_str(), &end);
        if (end != token.c_str() + token.size()) return std::nullopt;
        return Value(d);
    }
};

inline void AppendEscaped(std::wstring& out, std::wstring_view s) {
    out.push_back(L'"');
    for (const wchar_t c : s) {
        switch (c) {
            case L'"': out += L"\\\""; break;
            case L'\\': out += L"\\\\"; break;
            case L'\b': out += L"\\b"; break;
            case L'\f': out += L"\\f"; break;
            case L'\n': out += L"\\n"; break;
            case L'\r': out += L"\\r"; break;
            case L'\t': out += L"\\t"; break;
            default:
                if (c < 0x20) {
                    static const wchar_t kHex[] = L"0123456789abcdef";
                    out += L"\\u00";
                    out.push_back(kHex[(c >> 4) & 0xF]);
                    out.push_back(kHex[c & 0xF]);
                } else {
                    out.push_back(c);
                }
        }
    }
    out.push_back(L'"');
}

inline void AppendNumber(std::wstring& out, double d) {
    wchar_t buf[64];
    if (d == static_cast<double>(static_cast<long long>(d))) {
        ::swprintf_s(buf, L"%lld", static_cast<long long>(d));
    } else {
        ::swprintf_s(buf, L"%.17g", d);
    }
    out += buf;
}

inline void Write(std::wstring& out, const Value& v, bool pretty, int indent) {
    const auto newlineIndent = [&](int level) {
        if (!pretty) return;
        out.push_back(L'\n');
        out.append(static_cast<size_t>(level) * 2, L' ');
    };

    switch (v.type()) {
        case Type::Null: out += L"null"; break;
        case Type::Bool: out += v.AsBool() ? L"true" : L"false"; break;
        case Type::Number: AppendNumber(out, v.AsNumber()); break;
        case Type::String: AppendEscaped(out, v.AsString()); break;
        case Type::Array: {
            if (v.size() == 0) {
                out += L"[]";
                break;
            }
            out.push_back(L'[');
            for (size_t i = 0; i < v.size(); ++i) {
                if (i) out.push_back(L',');
                newlineIndent(indent + 1);
                Write(out, *v.At(i), pretty, indent + 1);
            }
            newlineIndent(indent);
            out.push_back(L']');
            break;
        }
        case Type::Object: {
            if (v.members().empty()) {
                out += L"{}";
                break;
            }
            out.push_back(L'{');
            bool first = true;
            for (const auto& [k, child] : v.members()) {
                if (!first) out.push_back(L',');
                first = false;
                newlineIndent(indent + 1);
                AppendEscaped(out, k);
                out.push_back(L':');
                if (pretty) out.push_back(L' ');
                Write(out, *child, pretty, indent + 1);
            }
            newlineIndent(indent);
            out.push_back(L'}');
            break;
        }
    }
}

}  // namespace detail

// Returns nullopt on any malformed input. Never throws.
inline std::optional<Value> Parse(std::wstring_view text) {
    detail::Parser p(text);
    return p.ParseDocument();
}

inline std::wstring Serialize(const Value& v, bool pretty = true) {
    std::wstring out;
    detail::Write(out, v, pretty, 0);
    if (pretty) out.push_back(L'\n');
    return out;
}

}  // namespace lfs::json
