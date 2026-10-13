#include "client/ui/ChatLog.hpp"
#include "client/ui/McFont.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>

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
                if (rest[i] == ' ') {
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

    void ChatLog::AddMessage(const std::string& text, uint64_t tick) {
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
    }

    void ChatLog::Close() {
        open = false;
        input.clear();
        caret = 0;
        scroll = 0;
        historyIndex = -1;
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

    ChatLog::Result ChatLog::Type(const std::vector<InputManager::Typed>& typed, float wheel) {
        Result r;
        if (wheel != 0.0f) {

            const int step = static_cast<int>(std::lround(wheel > 0 ? std::max(1.0f, wheel) : std::min(-1.0f, wheel)));
            scroll = std::clamp(scroll + step, 0, std::max(0, static_cast<int>(lines.size()) - 20));
        }
        for (const auto& t : typed) {
            if (t.character) {
                Insert(static_cast<uint32_t>(t.key));
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
            s.chatLines.push_back(std::move(v));
        }
        s.chatScrolled = scroll > 0;
        s.chatTotalLines = total;
        s.chatScroll = scroll;
        s.chatInput = input;
        s.chatCaret = static_cast<int>(caret);
        s.chatCaretVisible = ((nowMs - openedAt) / 300) % 2 == 0;
    }
}
