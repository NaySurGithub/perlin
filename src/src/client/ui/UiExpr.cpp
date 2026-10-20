#include "client/ui/UiExpr.hpp"

#include <cctype>
#include <cmath>
#include <cstdlib>

namespace perlin::ui {
    namespace {
        struct Lexer {
            std::string_view s;
            size_t pos = 0;

            void Skip() {
                while (pos < s.size() && std::isspace(static_cast<unsigned char>(s[pos])))
                    pos++;
            }
        };

        class Evaluator {
        public:
            Evaluator(std::string_view text, const ExprEnv& env) : lex{text, 0}, env(env) {}

            bool Run(Json& out) {
                Json v = Or();
                lex.Skip();
                if (failed || lex.pos < lex.s.size()) {
                    out = Json();
                    return false;
                }
                out = std::move(v);
                return true;
            }

        private:
            Lexer lex;
            const ExprEnv& env;
            bool failed = false;

            bool Match(std::string_view token) {
                lex.Skip();
                if (lex.s.compare(lex.pos, token.size(), token) == 0) {
                    lex.pos += token.size();
                    return true;
                }
                return false;
            }

            bool MatchWord(std::string_view word) {
                lex.Skip();
                if (lex.s.compare(lex.pos, word.size(), word) != 0)
                    return false;
                const size_t end = lex.pos + word.size();
                if (end < lex.s.size() && (std::isalnum(static_cast<unsigned char>(lex.s[end])) || lex.s[end] == '_'))
                    return false;
                lex.pos = end;
                return true;
            }

            static bool Equal(const Json& a, const Json& b) {
                if (a.IsNumber() && b.IsNumber())
                    return a.number == b.number;
                if (a.IsBool() && b.IsBool())
                    return a.boolean == b.boolean;
                if (a.IsString() && b.IsString())
                    return a.string == b.string;
                if (a.IsNull() && b.IsNull())
                    return true;
                if (a.IsNull() || b.IsNull())
                    return false;
                return a.Dump() == b.Dump();
            }

            static double AsNumber(const Json& v) {
                if (v.IsNumber()) return v.number;
                if (v.IsBool()) return v.boolean ? 1.0 : 0.0;
                if (v.IsString()) return std::atof(v.string.c_str());
                return 0.0;
            }

            Json Or() {
                Json left = And();
                while (MatchWord("or") || Match("||")) {
                    Json right = And();
                    left = Json(Truthy(left) || Truthy(right));
                }
                return left;
            }

            Json And() {
                Json left = Compare();
                while (MatchWord("and") || Match("&&")) {
                    Json right = Compare();
                    left = Json(Truthy(left) && Truthy(right));
                }
                return left;
            }

            Json Compare() {
                Json left = Sum();
                for (;;) {
                    if (Match("==")) left = Json(Equal(left, Sum()));
                    else if (Match("!=")) left = Json(!Equal(left, Sum()));
                    else if (Match(">=")) left = Json(AsNumber(left) >= AsNumber(Sum()));
                    else if (Match("<=")) left = Json(AsNumber(left) <= AsNumber(Sum()));
                    else if (Match(">")) left = Json(AsNumber(left) > AsNumber(Sum()));
                    else if (Match("<")) left = Json(AsNumber(left) < AsNumber(Sum()));
                    else if (lex.Skip(), lex.pos < lex.s.size() && lex.s[lex.pos] == '=' &&
                             !(lex.pos + 1 < lex.s.size() && lex.s[lex.pos + 1] == '=')) {
                        lex.pos++;
                        left = Json(Equal(left, Sum()));
                    } else {
                        return left;
                    }
                }
            }

            Json Sum() {
                Json left = Product();
                for (;;) {
                    lex.Skip();
                    if (lex.pos < lex.s.size() && lex.s[lex.pos] == '+') {
                        lex.pos++;
                        Json right = Product();
                        if (left.IsString() || right.IsString())
                            left = Json((left.IsString() ? left.string : left.Dump()) +
                                        (right.IsString() ? right.string : right.Dump()));
                        else
                            left = Json(AsNumber(left) + AsNumber(right));
                    } else if (lex.pos < lex.s.size() && lex.s[lex.pos] == '-') {
                        lex.pos++;
                        left = Json(AsNumber(left) - AsNumber(Product()));
                    } else {
                        return left;
                    }
                }
            }

            Json Product() {
                Json left = Unary();
                for (;;) {
                    lex.Skip();
                    if (lex.pos < lex.s.size() && lex.s[lex.pos] == '*') {
                        lex.pos++;
                        left = Json(AsNumber(left) * AsNumber(Unary()));
                    } else if (lex.pos < lex.s.size() && lex.s[lex.pos] == '/') {
                        lex.pos++;
                        const double d = AsNumber(Unary());
                        left = Json(d == 0 ? 0.0 : AsNumber(left) / d);
                    } else if (lex.pos < lex.s.size() && lex.s[lex.pos] == '%') {
                        lex.pos++;
                        const double d = AsNumber(Unary());
                        left = Json(d == 0 ? 0.0 : std::fmod(AsNumber(left), d));
                    } else {
                        return left;
                    }
                }
            }

            Json Unary() {
                if (MatchWord("not") || (lex.Skip(), lex.pos < lex.s.size() && lex.s[lex.pos] == '!' &&
                                                         !(lex.pos + 1 < lex.s.size() && lex.s[lex.pos + 1] == '=') &&
                                                         (lex.pos++, true)))
                    return Json(!Truthy(Unary()));
                lex.Skip();
                if (lex.pos < lex.s.size() && lex.s[lex.pos] == '-') {
                    lex.pos++;
                    return Json(-AsNumber(Unary()));
                }
                return Primary();
            }

            Json Primary() {
                lex.Skip();
                if (lex.pos >= lex.s.size()) {
                    failed = true;
                    return {};
                }
                const char c = lex.s[lex.pos];
                if (c == '(') {
                    lex.pos++;
                    Json v = Or();
                    lex.Skip();
                    if (lex.pos < lex.s.size() && lex.s[lex.pos] == ')')
                        lex.pos++;
                    else
                        failed = true;
                    return v;
                }
                if (c == '\'' || c == '"') {
                    const char quote = c;
                    lex.pos++;
                    std::string text;
                    while (lex.pos < lex.s.size() && lex.s[lex.pos] != quote)
                        text += lex.s[lex.pos++];
                    if (lex.pos < lex.s.size())
                        lex.pos++;
                    else
                        failed = true;
                    return Json(std::move(text));
                }
                if (std::isdigit(static_cast<unsigned char>(c)) || c == '.') {
                    const size_t start = lex.pos;
                    while (lex.pos < lex.s.size() &&
                           (std::isdigit(static_cast<unsigned char>(lex.s[lex.pos])) || lex.s[lex.pos] == '.'))
                        lex.pos++;
                    return Json(std::atof(std::string(lex.s.substr(start, lex.pos - start)).c_str()));
                }
                if (c == '#' || c == '$') {
                    const size_t start = lex.pos++;
                    while (lex.pos < lex.s.size() &&
                           (std::isalnum(static_cast<unsigned char>(lex.s[lex.pos])) || lex.s[lex.pos] == '_' ||
                            lex.s[lex.pos] == '.'))
                        lex.pos++;
                    const std::string name(lex.s.substr(start, lex.pos - start));
                    Json value;
                    if (c == '#' && env.binding && env.binding(name, value))
                        return value;
                    if (c == '$' && env.variable && env.variable(name, value))
                        return value;
                    return {};
                }
                if (MatchWord("true")) return Json(true);
                if (MatchWord("false")) return Json(false);
                if (MatchWord("null")) return Json();
                if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
                    const size_t start = lex.pos;
                    while (lex.pos < lex.s.size() &&
                           (std::isalnum(static_cast<unsigned char>(lex.s[lex.pos])) || lex.s[lex.pos] == '_' ||
                            lex.s[lex.pos] == '.'))
                        lex.pos++;
                    return Json(std::string(lex.s.substr(start, lex.pos - start)));
                }
                failed = true;
                return {};
            }
        };
    }

    bool IsExpression(std::string_view text) {
        size_t a = 0, b = text.size();
        while (a < b && std::isspace(static_cast<unsigned char>(text[a]))) a++;
        while (b > a && std::isspace(static_cast<unsigned char>(text[b - 1]))) b--;
        return b - a >= 2 && text[a] == '(' && text[b - 1] == ')';
    }

    bool UsesBindings(std::string_view text) {
        return text.find('#') != std::string_view::npos;
    }

    bool Truthy(const Json& v) {
        switch (v.type) {
        case Json::Type::Bool: return v.boolean;
        case Json::Type::Number: return v.number != 0.0;
        case Json::Type::String: return !v.string.empty() && v.string != "false" && v.string != "0";
        case Json::Type::Array: return !v.array.empty();
        case Json::Type::Object: return !v.object.empty();
        default: return false;
        }
    }

    bool EvalExpression(std::string_view text, const ExprEnv& env, Json& out) {
        Evaluator evaluator(text, env);
        return evaluator.Run(out);
    }
}
