#include "client/ui/ChatLog.hpp"
#include "client/ui/McFont.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <set>

namespace perlin::ui {
    namespace {
        bool Utf8Lead(unsigned char c) { return (c & 0xC0) != 0x80; }

        size_t PrevChar(const std::string& s, size_t pos) {
            if (pos == 0)
                return 0;
            do {
                pos--;
            } while (pos > 0 && !Utf8Lead(static_cast<unsigned char>(s[pos])));
            return pos;
        }

        size_t NextChar(const std::string& s, size_t pos) {
            if (pos >= s.size())
                return s.size();
            do {
                pos++;
            } while (pos < s.size() && !Utf8Lead(static_cast<unsigned char>(s[pos])));
            return pos;
        }

        void AppendUtf8(std::string& out, uint32_t cp) {
            if (cp < 0x80) {
                out += static_cast<char>(cp);
            } else if (cp < 0x800) {
                out += static_cast<char>(0xC0 | (cp >> 6));
                out += static_cast<char>(0x80 | (cp & 0x3F));
            } else if (cp < 0x10000) {
                out += static_cast<char>(0xE0 | (cp >> 12));
                out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                out += static_cast<char>(0x80 | (cp & 0x3F));
            } else {
                out += static_cast<char>(0xF0 | (cp >> 18));
                out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
                out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                out += static_cast<char>(0x80 | (cp & 0x3F));
            }
        }

        size_t Utf8Count(const std::string& s) {
            size_t n = 0;
            for (unsigned char c : s)
                if (Utf8Lead(c))
                    n++;
            return n;
        }
    }

    std::vector<std::string> ChatLog::Wrap(const std::string& text, int width) {
        std::vector<std::string> out;
        const McFont& font = McFont::Instance();
        std::string rest = text;
        std::string carry;
        while (true) {
            const std::string full = carry + rest;
            if (font.Width(full) <= static_cast<float>(width) || rest.empty()) {
                out.push_back(full);
                break;
            }
            size_t fit = 0, lastSpace = std::string::npos;
            int bracket = 0;
            for (size_t i = 0; i < rest.size();) {
                size_t next;
                if (rest.compare(i, 2, "\xC2\xA7") == 0 && i + 2 < rest.size()) {
                    next = i + 3;
                } else {
                    next = NextChar(rest, i);
                }
                const std::string candidate = carry + rest.substr(0, next);
                if (font.Width(candidate) > static_cast<float>(width))
                    break;
                fit = next;
                if (rest[i] == '[')
                    bracket++;
                else if (rest[i] == ']' && bracket > 0)
                    bracket--;
                if (rest[i] == ' ' && bracket == 0) {
                    lastSpace = next;
                }
                i = next;
            }
            if (fit == 0)
                fit = NextChar(rest, 0);
            size_t cut = fit;
            if (lastSpace != std::string::npos && fit < rest.size())
                cut = lastSpace;
            const std::string head = rest.substr(0, cut);
            out.push_back(carry + head);
            std::string state = carry;
            for (size_t i = 0; i + 2 < head.size(); i++) {
                if (head.compare(i, 2, "\xC2\xA7") != 0)
                    continue;
                const char code = static_cast<char>(std::tolower(static_cast<unsigned char>(head[i + 2])));
                if (code == 'r')
                    state.clear();
                else if ((code >= '0' && code <= '9') || (code >= 'a' && code <= 'f'))
                    state = std::string("\xC2\xA7") + code;
                else
                    state += std::string("\xC2\xA7") + code;
                i += 2;
            }
            carry = state;
            rest = rest.substr(cut);
            if (rest.empty())
                break;
        }
        return out;
    }

    void ChatLog::AddMessage(const std::string& original, uint64_t tick) {
        std::string text = original;
        if (text.find(" is at ") != std::string::npos && text.rfind('<', 0) != 0 && text.find("\xC2\xA7" "a[") == std::string::npos) {
            const auto found = FindCoordinates(text);
            for (auto it = found.rbegin(); it != found.rend(); ++it)
                text.replace(it->begin, it->end - it->begin, "\xC2\xA7" "a[" + it->x + ", " + it->y + ", " + it->z + "]\xC2\xA7" "r");
        }
        for (const std::string& piece : Wrap(text, kWidth)) {
            if (scroll > 0)
                scroll++;
            lines.push_back({piece, tick});
        }
        while (lines.size() > kMaxLines)
            lines.pop_front();
        scroll = std::min(scroll, std::max(0, static_cast<int>(lines.size()) - 20));
    }

    void ChatLog::Open(const std::string& prefill, uint64_t nowMs) {
        open = true;
        openedAt = nowMs;
        input = prefill;
        caret = input.size();
        historyIndex = -1;
        scroll = 0;
        Recompute();
    }

    void ChatLog::Close() {
        open = false;
        input.clear();
        caret = 0;
        scroll = 0;
        historyIndex = -1;
        suggestions.clear();
        hint.clear();
    }

    void ChatLog::Insert(uint32_t cp) {
        if (cp < 32 || cp == 127 || cp == 0xA7)
            return;
        if (Utf8Count(input) >= kMaxInput)
            return;
        std::string chunk;
        AppendUtf8(chunk, cp);
        input.insert(caret, chunk);
        caret += chunk.size();
    }

    void ChatLog::MoveCaret(int delta, bool word) {
        for (int n = 0; n < std::abs(delta); n++) {
            if (delta < 0) {
                if (word) {
                    while (caret > 0 && input[caret - 1] == ' ')
                        caret = PrevChar(input, caret);
                    while (caret > 0 && input[caret - 1] != ' ')
                        caret = PrevChar(input, caret);
                } else {
                    caret = PrevChar(input, caret);
                }
            } else {
                if (word) {
                    while (caret < input.size() && input[caret] != ' ')
                        caret = NextChar(input, caret);
                    while (caret < input.size() && input[caret] == ' ')
                        caret = NextChar(input, caret);
                } else {
                    caret = NextChar(input, caret);
                }
            }
        }
    }

    void ChatLog::Erase(bool forward, bool word) {
        const size_t from = caret;
        MoveCaret(forward ? 1 : -1, word);
        const size_t a = std::min(from, caret), b = std::max(from, caret);
        input.erase(a, b - a);
        caret = a;
    }

    void ChatLog::Recall(int direction) {
        if (history.empty())
            return;
        if (historyIndex == -1) {
            if (direction > 0)
                return;
            draft = input;
            historyIndex = static_cast<int>(history.size());
        }
        const int next = std::clamp(historyIndex + direction, 0, static_cast<int>(history.size()));
        if (next == historyIndex)
            return;
        historyIndex = next;
        if (historyIndex >= static_cast<int>(history.size())) {
            input = draft;
            historyIndex = -1;
        } else {
            input = history[static_cast<size_t>(historyIndex)];
        }
        caret = input.size();
    }

    std::vector<ChatLog::Coordinate> ChatLog::FindCoordinates(const std::string& line) {
        std::vector<Coordinate> out;
        const size_t n = line.size();
        const auto isDigit = [&](size_t i) { return i < n && std::isdigit(static_cast<unsigned char>(line[i])); };
        const auto skipSpace = [&](size_t i) { while (i < n && line[i] == ' ') i++; return i; };
        const auto number = [&](size_t i, std::string& text) -> size_t {
            size_t j = i;
            if (j < n && line[j] == '-')
                j++;
            if (!isDigit(j))
                return std::string::npos;
            while (isDigit(j))
                j++;
            text = line.substr(i, j - i);
            return j;
        };
        for (size_t i = 0; i < n;) {
            if (!(isDigit(i) || (line[i] == '-' && isDigit(i + 1))) || (i > 0 && (isDigit(i - 1) || std::isalpha(static_cast<unsigned char>(line[i - 1]))))) {
                i++;
                continue;
            }
            std::string x, y = "~", z;
            size_t j = number(i, x);
            size_t k = skipSpace(j);
            if (k >= n || line[k] != ',') { i = j; continue; }
            k = skipSpace(k + 1);
            size_t m = std::string::npos;
            std::string mid;
            if (k < n && line.compare(k, 4, "(y?)") == 0) { m = k + 4; }
            else if (k < n && line[k] == '~') { m = k + 1; }
            else { m = number(k, mid); if (m != std::string::npos) y = mid; }
            if (m == std::string::npos) { i = j; continue; }
            size_t q = skipSpace(m);
            if (q >= n || line[q] != ',') { i = j; continue; }
            q = skipSpace(q + 1);
            const size_t e = number(q, z);
            if (e == std::string::npos) { i = j; continue; }
            Coordinate c;
            c.begin = i;
            c.end = e;
            c.x = x; c.y = y; c.z = z;
            c.command = "/tp @s " + x + " " + y + " " + z;
            out.push_back(std::move(c));
            i = e;
        }
        return out;
    }

    namespace {
        std::string Lower(std::string s) {
            for (char& c : s)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return s;
        }
        bool Matches(const std::string& candidateLower, const std::string& prefixLower) {
            if (candidateLower.rfind(prefixLower, 0) == 0)
                return true;
            const size_t colon = candidateLower.find(':');
            return colon != std::string::npos && candidateLower.compare(colon + 1, prefixLower.size(), prefixLower) == 0;
        }
    }

    void ChatLog::Recompute() {
        suggestions.clear();
        hint.clear();
        suggestIndex = 0;
        suggestOffset = 0;
        suggestWordStart = caret;
        if (!open || input.empty() || input[0] != '/' || caret == 0)
            return;
        const size_t sp = caret > 0 ? input.find_last_of(' ', caret - 1) : std::string::npos;
        const size_t ws = sp == std::string::npos ? 1 : sp + 1;
        if (ws > caret)
            return;
        suggestWordStart = ws;
        const std::string prefix = Lower(input.substr(ws, caret - ws));
        std::vector<std::string> found;
        if (ws == 1) {
            for (const CommandInfo& c : commands)
                if (Matches(Lower(c.name), prefix))
                    found.push_back(c.name);
        } else {
            std::vector<std::string> tokens;
            {
                size_t i = 1;
                while (i < ws) {
                    while (i < ws && input[i] == ' ') i++;
                    const size_t b = i;
                    while (i < ws && input[i] != ' ') i++;
                    if (i > b)
                        tokens.push_back(input.substr(b, i - b));
                }
            }
            if (tokens.empty())
                return;
            const size_t argIndex = tokens.size() - 1;
            const std::string name = Lower(tokens[0]);
            std::set<std::string> seen;
            for (const CommandInfo& c : commands) {
                if (Lower(c.name) != name)
                    continue;
                for (const auto& overload : c.overloads) {
                    if (argIndex >= overload.size())
                        continue;
                    const CommandParam& p = overload[argIndex];
                    if (hint.empty())
                        hint = p.optional ? "[" + p.name + "]" : "<" + p.name + ">";
                    auto offer = [&](const std::string& v) {
                        if (Matches(Lower(v), prefix) && seen.insert(v).second)
                            found.push_back(v);
                    };
                    for (const std::string& v : p.values)
                        offer(v);
                    if (p.target) {
                        for (const char* sel : {"@s", "@p", "@a", "@r", "@e", "@initiator"})
                            offer(sel);
                        for (const std::string& pl : players)
                            offer(pl.find(' ') == std::string::npos ? pl : "\"" + pl + "\"");
                    }
                }
            }
        }
        std::sort(found.begin(), found.end(), [&](const std::string& a, const std::string& b) {
            const bool pa = Lower(a).rfind(prefix, 0) == 0, pb = Lower(b).rfind(prefix, 0) == 0;
            if (pa != pb)
                return pa;
            return Lower(a) < Lower(b);
        });
        if (found.size() > 300)
            found.resize(300);
        if (found.size() == 1 && Lower(found[0]) == prefix)
            found.clear();
        suggestions = std::move(found);
        if (!suggestions.empty())
            hint.clear();
    }

    void ChatLog::ApplySuggestion(size_t index) {
        if (index >= suggestions.size())
            return;
        const std::string& value = suggestions[index];
        input.replace(suggestWordStart, caret - suggestWordStart, value);
        caret = suggestWordStart + value.size();
    }

    ChatLog::Result ChatLog::Type(const std::vector<InputManager::Typed>& typed, float wheel) {
        Result r;
        if (wheel != 0.0f) {
            const int step = static_cast<int>(std::lround(wheel > 0 ? std::max(1.0f, wheel) : std::min(-1.0f, wheel)));
            scroll = std::clamp(scroll + step, 0, std::max(0, static_cast<int>(lines.size()) - 20));
        }
        for (const auto& t : typed) {
            if (!t.character && (t.key == Key::Tab) && !suggestions.empty()) {
                if (cycling)
                    suggestIndex = (suggestIndex + 1) % suggestions.size();
                else
                    cycleBase = input;
                if (cycling) {
                    const std::string saved = suggestions[suggestIndex];
                    input.replace(suggestWordStart, caret - suggestWordStart, saved);
                    caret = suggestWordStart + saved.size();
                } else {
                    ApplySuggestion(suggestIndex);
                }
                cycling = true;
                continue;
            }
            if (!t.character && (t.key == Key::Up || t.key == Key::Down) && suggestions.size() > 1) {
                const size_t n = suggestions.size();
                suggestIndex = t.key == Key::Down ? (suggestIndex + 1) % n : (suggestIndex + n - 1) % n;
                cycling = false;
                continue;
            }
            cycling = false;
            if (t.character) {
                Insert(static_cast<uint32_t>(t.key));
                Recompute();
                continue;
            }
            switch (t.key) {
            case Key::Escape:
                r.close = true;
                return r;
            case Key::Enter: {
                r.close = true;
                r.send = !input.empty();
                r.text = input;
                if (r.send) {
                    if (history.empty() || history.back() != input)
                        history.push_back(input);
                    while (history.size() > kMaxHistory)
                        history.erase(history.begin());
                }
                return r;
            }
            case Key::Backspace: Erase(false, t.ctrl); break;
            case Key::Delete: Erase(true, t.ctrl); break;
            case Key::Left: MoveCaret(-1, t.ctrl); break;
            case Key::Right: MoveCaret(1, t.ctrl); break;
            case Key::Home: caret = 0; break;
            case Key::End: caret = input.size(); break;
            case Key::Up: Recall(-1); break;
            case Key::Down: Recall(1); break;
            case Key::PageUp: scroll = std::clamp(scroll + 19, 0, std::max(0, static_cast<int>(lines.size()) - 20)); break;
            case Key::PageDown: scroll = std::clamp(scroll - 19, 0, std::max(0, static_cast<int>(lines.size()) - 20)); break;
            default: break;
            }
            Recompute();
        }
        return r;
    }

    void ChatLog::Fill(HudState& s, uint64_t tick, uint64_t nowMs) const {
        s.chatOpen = open;
        s.chatLines.clear();
        const int maxLines = open ? 20 : 10;
        const int total = static_cast<int>(lines.size());
        for (int i = 0; i < maxLines && i + scroll < total; i++) {
            const Line& l = lines[static_cast<size_t>(total - 1 - (i + scroll))];
            const double age = static_cast<double>(tick - std::min(tick, l.added));
            float d = 1.0f;
            if (!open) {
                if (age >= 200.0)
                    continue;
                d = static_cast<float>(std::clamp((1.0 - age / 200.0) * 10.0, 0.0, 1.0));
                d *= d;
            }
            HudState::ChatLine v;
            v.text = l.text;
            v.alpha = d;
            v.row = i;
            if (open) {
                const McFont& font = McFont::Instance();
                for (const Coordinate& c : FindCoordinates(l.text)) {
                    size_t b = c.begin, e = c.end;
                    if (b > 0 && l.text[b - 1] == '[') b--;
                    if (e < l.text.size() && l.text[e] == ']') e++;
                    v.clicks.push_back({font.Width(l.text.substr(0, b)), font.Width(l.text.substr(b, e - b)), c.command});
                }
            }
            s.chatLines.push_back(std::move(v));
        }
        s.chatScrolled = scroll > 0;
        s.chatTotalLines = total;
        s.chatScroll = scroll;
        s.chatInput = input;
        s.chatCaret = static_cast<int>(caret);
        s.chatCaretVisible = ((nowMs - openedAt) / 300) % 2 == 0;
        s.chatSuggestions.clear();
        s.chatSuggestSelected = -1;
        s.chatSuggestStart = static_cast<int>(suggestWordStart);
        s.chatHint = hint;
        s.chatGhost.clear();
        s.chatSuggestMore[0] = s.chatSuggestMore[1] = false;
        if (open && !suggestions.empty()) {
            const size_t window = 10;
            if (suggestIndex < suggestOffset)
                suggestOffset = suggestIndex;
            else if (suggestIndex >= suggestOffset + window)
                suggestOffset = suggestIndex - window + 1;
            suggestOffset = std::min(suggestOffset, suggestions.size() > window ? suggestions.size() - window : 0);
            const size_t first = suggestOffset;
            for (size_t i = first; i < suggestions.size() && i < first + window; i++)
                s.chatSuggestions.push_back(suggestions[i]);
            s.chatSuggestMore[0] = first > 0;
            s.chatSuggestMore[1] = first + window < suggestions.size();
            const std::string typed = input.substr(suggestWordStart, caret - suggestWordStart);
            const std::string& chosen = suggestions[suggestIndex];
            if (chosen.size() > typed.size() && Lower(chosen.substr(0, typed.size())) == Lower(typed) && caret == input.size())
                s.chatGhost = chosen.substr(typed.size());
            else
                s.chatGhost.clear();
            s.chatSuggestSelected = static_cast<int>(suggestIndex - first);
        }
    }
}
