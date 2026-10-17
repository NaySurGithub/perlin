#include "client/ui/RmlEmit.hpp"
#include "client/ui/McFont.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace perlin::ui {
    namespace {
        int Px(float v) {
            return static_cast<int>(std::ceil(v - 0.5f));
        }

        std::string Num(float v) {
            char buffer[32];
            if (std::fabs(v - std::round(v)) < 0.0005f)
                std::snprintf(buffer, sizeof(buffer), "%d", static_cast<int>(std::lround(v)));
            else
                std::snprintf(buffer, sizeof(buffer), "%.3f", v);
            return buffer;
        }

        std::string Hex(float r, float g, float b, float a) {
            const auto byte = [](float v) { return std::clamp(static_cast<int>(std::lround(v * 255.0f)), 0, 255); };
            char buffer[16];
            std::snprintf(buffer, sizeof(buffer), "#%02x%02x%02x%02x", byte(r), byte(g), byte(b), byte(a));
            return buffer;
        }

        const char* LegacyColor(char code) {
            switch (code) {
            case '0': return "#000000"; case '1': return "#0000aa"; case '2': return "#00aa00"; case '3': return "#00aaaa";
            case '4': return "#aa0000"; case '5': return "#aa00aa"; case '6': return "#ffaa00"; case '7': return "#aaaaaa";
            case '8': return "#555555"; case '9': return "#5555ff"; case 'a': case 'A': return "#55ff55";
            case 'b': case 'B': return "#55ffff"; case 'c': case 'C': return "#ff5555"; case 'd': case 'D': return "#ff55ff";
            case 'e': case 'E': return "#ffff55"; case 'f': case 'F': return "#ffffff";
            default: return nullptr;
            }
        }

        std::string FormatLine(const std::string& line, const TextStyle& style) {
            std::string out;
            std::string pendingEscape;
            int open = 0;
            const auto closeAll = [&] {
                while (open > 0) {
                    out += "</span>";
                    open--;
                }
            };
            size_t i = 0;
            std::string chunk;
            const auto flush = [&] {
                out += EscapeRml(chunk);
                chunk.clear();
            };
            const std::string& full = line;
            while (i < full.size()) {
                const unsigned char c = static_cast<unsigned char>(full[i]);
                if (c == 0xC2 && i + 2 < full.size() && static_cast<unsigned char>(full[i + 1]) == 0xA7) {
                    const char code = full[i + 2];
                    flush();
                    if (const char* color = LegacyColor(code)) {
                        closeAll();
                        std::string rgba = color;
                        const int a = std::clamp(static_cast<int>(std::lround(style.a * 255.0f)), 0, 255);
                        char hex[4];
                        std::snprintf(hex, sizeof(hex), "%02x", a);
                        rgba += hex;
                        out += "<span style=\"color:" + rgba + ";\">";
                        open++;
                    } else if (code == 'l' || code == 'L') {
                        out += "<span style=\"font-weight:bold;\">";
                        open++;
                    } else if (code == 'r' || code == 'R') {
                        closeAll();
                    }
                    i += 3;
                    continue;
                }
                chunk += full[i++];
            }
            flush();
            closeAll();
            return out;
        }
    }

    std::string EscapeRml(const std::string& text) {
        std::string out;
        out.reserve(text.size());
        for (const char c : text) {
            switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            default: out += c;
            }
        }
        return out;
    }

    void RmlWriter::Comment(const std::string& text) {
        if (!comments)
            return;
        body += "  <!-- " + text + " -->\n";
    }

    void RmlWriter::Image(float ax, float ay, float w, float h, int z, const std::string& texture, const ImageStyle& st) {
        ax -= ox;
        ay -= oy;
        if (w <= 0 || h <= 0)
            return;
        count++;
        body += "  <mcimg src=\"" + EscapeRml(texture) + "\" style=\"left:" + std::to_string(Px(ax)) + "px;top:" +
                std::to_string(Px(ay)) + "px;width:" + Num(w) + "px;height:" + Num(h) + "px;z-index:" + std::to_string(z) + ";\"";
        if (st.u != 0 || st.v != 0 || st.uw >= 0 || st.vh >= 0)
            body += " uv=\"" + Num(st.u) + " " + Num(st.v) + " " + Num(st.uw) + " " + Num(st.vh) + "\"";
        if (st.hasNine)
            body += " nine=\"" + Num(st.nine[0]) + " " + Num(st.nine[1]) + " " + Num(st.nine[2]) + " " + Num(st.nine[3]) + "\"";
        if (st.tiled)
            body += " tiled=\"" + std::to_string(st.tiled) + "\"";
        if (st.tileScale != 1.0f)
            body += " tilescale=\"" + Num(st.tileScale) + "\"";
        if (!st.clipDirection.empty() && st.clipRatio < 1.0f)
            body += " clip=\"" + st.clipDirection + " " + Num(st.clipRatio) + "\"";
        if (st.r != 1.0f || st.g != 1.0f || st.b != 1.0f)
            body += " color=\"" + Num(st.r) + " " + Num(st.g) + " " + Num(st.b) + "\"";
        if (st.alpha != 1.0f)
            body += " alpha=\"" + Num(st.alpha) + "\"";
        if (st.keepRatio)
            body += " keep=\"1\"";
        body += "/>\n";
    }

    void RmlWriter::Link(float ax, float ay, float w, float h, int z, const std::string& command) {
        ax -= ox;
        ay -= oy;
        count++;
        body += "  <div class=\"link\" id=\"link:" + EscapeRml(command) + "\" style=\"left:" + Num(ax) + "px;top:" + Num(ay) + "px;width:" +
                Num(w) + "px;height:" + Num(h) + "px;z-index:" + std::to_string(z) + ";\"><div class=\"ul\"></div></div>\n";
    }

    void RmlWriter::BeginBox(float ax, float ay, float w, float h, int z, const std::string& cls, const std::string& id, bool clip, float scroll) {
        count++;
        body += "  <div" + (cls.empty() ? std::string() : " class=\"" + cls + "\"") + (id.empty() ? std::string() : " id=\"" + EscapeRml(id) + "\"") +
                " style=\"position:absolute;display:block;left:" + Num(ax - ox) + "px;top:" + Num(ay - oy) + "px;width:" + Num(w) + "px;height:" + Num(h) +
                "px;z-index:" + std::to_string(z) + ";" + (clip ? "overflow:hidden;" : "") + "\">\n";
        origins.emplace_back(ox, oy);
        ox = ax;
        oy = ay + scroll;
    }

    void RmlWriter::EndBox() {
        if (origins.empty())
            return;
        body += "  </div>\n";
        ox = origins.back().first;
        oy = origins.back().second;
        origins.pop_back();
    }

    void RmlWriter::BeginSection() {
        saved = std::move(body);
        body.clear();
    }

    void RmlWriter::EndSection(const std::string& name) {
        sections[name] = std::move(body);
        body = std::move(saved);
        saved.clear();
    }

    void RmlWriter::Rect(float ax, float ay, float w, float h, int z, float r, float g, float b, float a) {
        ImageStyle st;
        st.r = r;
        st.g = g;
        st.b = b;
        st.alpha = a;
        Image(ax, ay, w, h, z, "mc:white", st);
    }

    void RmlWriter::Item(float ax, float ay, float w, float h, int z, const std::string& itemKey, float alpha, bool glint) {
        ax -= ox;
        ay -= oy;
        count++;
        body += "  <mcitem key=\"" + EscapeRml(itemKey) + "\" style=\"left:" + std::to_string(Px(ax)) + "px;top:" +
                std::to_string(Px(ay)) + "px;width:" + Num(w) + "px;height:" + Num(h) + "px;z-index:" + std::to_string(z) + ";\"";
        if (alpha != 1.0f)
            body += " alpha=\"" + Num(alpha) + "\"";
        if (glint)
            body += " glint=\"1\"";
        body += "/>\n";
    }

    void RmlWriter::Text(float ax, float ay, float w, float h, int z, const std::vector<std::string>& lines,
                         const TextStyle& st) {
        if (lines.empty() || st.a <= 0.0f)
            return;
        ax -= ox;
        ay -= oy;
        count++;
        const float size = 8.0f * st.scale;
        const float lineHeight = 9.0f * st.scale + st.linePadding;
        body += "  <div class=\"t" + std::string(st.outline ? " o" : (st.shadow ? " s" : "")) + "\" style=\"left:" + std::to_string(Px(ax)) + "px;top:" +
                std::to_string(Px(ay)) + "px;width:" + Num(std::max(w, 1.0f)) + "px;height:" + Num(h) + "px;z-index:" +
                std::to_string(z) + ";font-size:" + Num(size) + "px;line-height:" + Num(lineHeight) + "px;text-align:" +
                st.align + ";color:" + Hex(st.r, st.g, st.b, st.a) + ";\">";
        for (size_t i = 0; i < lines.size(); i++) {
            if (i)
                body += "<br/>";
            body += FormatLine(lines[i], st);
        }
        body += "</div>\n";
    }

    namespace {
        bool ReadColor(const Json* v, float& r, float& g, float& b, float* a = nullptr) {
            if (!v || !v->IsArray() || v->array.size() < 3)
                return false;
            r = static_cast<float>(v->array[0].NumberOr(1));
            g = static_cast<float>(v->array[1].NumberOr(1));
            b = static_cast<float>(v->array[2].NumberOr(1));
            if (a && v->array.size() >= 4)
                *a = static_cast<float>(v->array[3].NumberOr(1));
            return true;
        }
    }

    void RmlEmitter::EmitImage(const LNode& n, RmlWriter& out) {
        std::string texture = n.Str("texture");
        if (const Json* t = n.bag.Find("#texture"); t && t->IsString())
            texture = t->string;
        if (texture.empty() || texture[0] == '$' || texture[0] == '#')
            return;
        ImageStyle st;
        if (const Json* uv = n.Get("uv"); uv && uv->IsArray() && uv->array.size() >= 2) {
            st.u = static_cast<float>(uv->array[0].NumberOr(0));
            st.v = static_cast<float>(uv->array[1].NumberOr(0));
        }
        if (const Json* uv = n.Get("uv_size"); uv && uv->IsArray() && uv->array.size() >= 2) {
            st.uw = static_cast<float>(uv->array[0].NumberOr(-1));
            st.vh = static_cast<float>(uv->array[1].NumberOr(-1));
        }
        if (const Json* ns = n.Get("nineslice_size")) {
            if (ns->IsNumber()) {
                st.hasNine = true;
                for (float& v : st.nine) v = static_cast<float>(ns->number);
            } else if (ns->IsArray() && ns->array.size() >= 4) {
                st.hasNine = true;
                for (int i = 0; i < 4; i++) st.nine[i] = static_cast<float>(ns->array[static_cast<size_t>(i)].NumberOr(0));
            } else if (ns->IsArray() && ns->array.size() == 2) {
                st.hasNine = true;
                st.nine[0] = st.nine[2] = static_cast<float>(ns->array[0].NumberOr(0));
                st.nine[1] = st.nine[3] = static_cast<float>(ns->array[1].NumberOr(0));
            }
        }
        if (const Json* t = n.Get("tiled")) {
            if (t->IsBool() && t->boolean) st.tiled = 1;
            else if (t->IsString()) st.tiled = t->string == "x" ? 2 : t->string == "y" ? 3 : (t->string == "true" ? 1 : 0);
        }
        st.tileScale = static_cast<float>(n.Num("tiled_scale", 1.0));
        st.clipDirection = n.Str("clip_direction", "left");
        float ratio = 1.0f;
        if (const Json* c = n.bag.Find("#clip_ratio"); c && c->IsNumber())
            ratio = static_cast<float>(c->number);
        else if (const Json* c2 = n.Get("clip_ratio"); c2 && c2->IsNumber())
            ratio = static_cast<float>(c2->number);
        st.clipRatio = std::clamp(ratio, 0.0f, 1.0f);
        ReadColor(n.Get("color"), st.r, st.g, st.b);
        st.alpha = n.alpha;
        st.keepRatio = n.Bool("keep_ratio", false);
        if (st.alpha <= 0.0f)
            return;
        out.Image(n.ax, n.ay, n.w, n.h, n.layer, texture, st);
    }

    void RmlEmitter::EmitLabel(const LNode& n, RmlWriter& out) {
        if (n.lines.empty())
            return;
        TextStyle st;
        float scale = static_cast<float>(n.Num("font_scale_factor", 1.0));
        const std::string size = n.Str("font_size", "normal");
        if (size == "small") scale *= 0.75f;
        else if (size == "large") scale *= 1.5f;
        else if (size == "extra_large") scale *= 2.0f;
        st.scale = scale;
        float a = 1.0f;
        ReadColor(n.Get("color"), st.r, st.g, st.b, &a);
        st.a = a * n.alpha;
        st.shadow = n.Bool("shadow", false);
        st.outline = n.Bool("perlin_text_outline", false);
        st.align = n.Str("text_alignment", "left");
        st.linePadding = static_cast<float>(n.Num("line_padding", 0));
        out.Text(n.ax, n.ay, n.w, n.h, n.layer, n.lines, st);
    }

    namespace {
        std::string ButtonCommand(const LNode& n) {
            const Json* maps = n.Get("button_mappings");
            if (!maps || !maps->IsArray())
                return {};
            for (const Json& m : maps->array) {
                if (!m.IsObject())
                    continue;
                const Json* to = m.Find("to_button_id");
                if (!to || !to->IsString())
                    continue;
                const std::string& id = to->string;
                if (id == "button.form_button_click")
                    return "fb:" + std::to_string(std::max(0, n.index));
                if (id == "button.menu_exit" || id == "button.menu_cancel" || id == "button.exit_dialog" || id.find("close") != std::string::npos)
                    return "close";
            }
            return {};
        }

        const LNode* FindKid(const LNode& n, const std::string& name) {
            if (name.empty())
                return nullptr;
            for (const auto& kid : n.kids)
                if (kid->id == name)
                    return kid.get();
            return nullptr;
        }
    }

    void RmlEmitter::VisitButton(const LNode& n, RmlWriter& out) {
        const std::string command = ButtonCommand(n);
        const std::string defName = n.Str("default_control"), hoverName = n.Str("hover_control"), pressedName = n.Str("pressed_control"),
                          lockedName = n.Str("locked_control");
        const bool hasHover = FindKid(n, hoverName) != nullptr && !command.empty();
        out.BeginBox(n.ax, n.ay, n.w, n.h, n.layer, "bt", command, false);
        for (const auto& kid : n.kids) {
            if (!kid->visible)
                continue;
            if (kid->id == defName) {
                out.BeginBox(n.ax, n.ay, n.w, n.h, 0, hasHover ? "df" : "", "", false);
                Visit(*kid, out);
                out.EndBox();
            } else if (kid->id == hoverName) {
                if (hasHover) {
                    out.BeginBox(n.ax, n.ay, n.w, n.h, 0, "hv", "", false);
                    Visit(*kid, out);
                    out.EndBox();
                }
            } else if (kid->id == pressedName || kid->id == lockedName) {
                continue;
            } else {
                Visit(*kid, out);
            }
        }
        out.EndBox();
    }

    void RmlEmitter::VisitScroll(const LNode& n, RmlWriter& out) {
        float content = 0;
        for (const auto& kid : n.kids)
            if (kid->id == contentName || content == 0)
                content = std::max(content, kid->h);
        contentHeight = content;
        viewportHeight = n.h;
        const float maxScroll = std::max(0.0f, content - n.h);
        const float s = std::clamp(scroll, 0.0f, maxScroll);
        out.BeginBox(n.ax, n.ay, n.w, n.h, n.layer, "", "", true, s);
        for (const auto& kid : n.kids)
            Visit(*kid, out);
        out.EndBox();
    }

    void RmlEmitter::Visit(const LNode& n, RmlWriter& out) {
        if (!n.visible)
            return;
        if (n.type == "button") {
            VisitButton(n, out);
            return;
        }
        if (n.type == "scroll_view") {
            viewportName = n.Str("scrolling_view_port", "scrolling_view_port");
            contentName = n.Str("scrolling_content", "scrolling_content");
        } else if (!viewportName.empty() && n.id == viewportName) {
            VisitScroll(n, out);
            return;
        }
        if (n.type == "image") {
            EmitImage(n, out);
        } else if (n.type == "label") {
            EmitLabel(n, out);
        } else if (n.type == "custom") {
            const std::string name = n.Str("renderer");
            auto it = renderers.find(name);
            if (it != renderers.end()) {
                it->second(n, out);
            } else if (std::find(unknown.begin(), unknown.end(), name) == unknown.end()) {
                unknown.push_back(name);
            }
        }
        for (const auto& kid : n.kids)
            Visit(*kid, out);
    }

    std::string DefaultHudTemplate() {
        return R"PERLINRML(<rml>
<head>
<title>hud</title>
<style>
/* Edit this file while the game runs: it is reloaded when it changes. {{name}} placeholders are filled in by Perlin. */
body { position: absolute; left: 0px; top: 0px; width: {{w}}px; height: {{h}}px; margin: 0; padding: 0; background: transparent; }
mcimg, mcitem { display: block; position: absolute; }
.t { display: block; position: absolute; font-family: minecraft; white-space: nowrap; overflow: visible; }
.t.s { font-family: minecraft-shadow; }
.t.o { font-family: minecraft-outline; }
{{chat_style}}
{{rcss}}
</style>
</head>
<body>
{{hud}}
{{chat}}
</body>
</rml>
)PERLINRML";
    }

    std::string DefaultChatTemplate() {
        return R"PERLINRML(<rml>
<head>
<title>chat</title>
<style>
/* The chat. Lines are generated (newest at the bottom) inside #chat-lines; change {{chat_top}} to move the whole block
   (it sits above the hearts / armor row by default). #chat-input is the bar shown while typing. */
#chat-lines { display: block; position: absolute; left: 0px; top: {{chat_top}}px; width: {{chat_w}}px; height: 180px; z-index: 30; }
.link { display: block; position: absolute; }
.link .ul { display: none; position: absolute; left: 0px; bottom: 0px; width: 100%; height: 1px; background-color: #ffff55; }
.link:hover .ul { display: block; }
#chat-scroll { display: block; position: absolute; left: 0px; top: {{chat_top}}px; width: 2px; height: 180px; z-index: 31; }
#chat-input { display: block; position: absolute; left: 2px; top: {{input_top}}px; width: {{input_w}}px; height: 12px; z-index: 32; }
</style>
</head>
<body>
  <div id="chat-lines">{{lines}}</div>
  <div id="chat-scroll">{{scrollbar}}</div>
  <div id="chat-input">{{input}}</div>
</body>
</rml>
)PERLINRML";
    }

    namespace {
        void Replace(std::string& text, const std::string& key, const std::string& value) {
            const std::string needle = "{{" + key + "}}";
            for (size_t at = text.find(needle); at != std::string::npos; at = text.find(needle, at + value.size()))
                text.replace(at, needle.size(), value);
        }
        std::string Between(const std::string& doc, const std::string& tag) {
            const size_t open = doc.find("<" + tag);
            if (open == std::string::npos)
                return {};
            const size_t start = doc.find('>', open);
            const size_t end = doc.find("</" + tag + ">", start == std::string::npos ? open : start);
            if (start == std::string::npos || end == std::string::npos)
                return {};
            return doc.substr(start + 1, end - start - 1);
        }
    }

    std::string RmlEmitter::Emit(const LNode& root, float guiW, float guiH, const std::string& extraRcss, bool comments) {
        RmlWriter writer(comments);
        unknown.clear();
        viewportName.clear();
        contentName.clear();
        contentHeight = viewportHeight = 0;
        Visit(root, writer);

        std::string chatStyle, chatBody;
        const auto& sec = writer.Sections();
        if (sec.count("chat_lines")) {
            std::string chat = chatTemplate.empty() ? DefaultChatTemplate() : chatTemplate;
            chatStyle = Between(chat, "style");
            chatBody = Between(chat, "body");
            const auto get = [&](const char* name) { const auto it = sec.find(name); return it == sec.end() ? std::string() : it->second; };
            for (std::string* part : {&chatStyle, &chatBody}) {
                Replace(*part, "w", Num(guiW));
                Replace(*part, "h", Num(guiH));
                Replace(*part, "chat_top", Num(guiH - 60.0f - 180.0f));
                Replace(*part, "chat_w", Num(328.0f));
                Replace(*part, "input_top", Num(guiH - 14.0f));
                Replace(*part, "input_w", Num(guiW - 4.0f));
            }
            Replace(chatBody, "lines", get("chat_lines"));
            Replace(chatBody, "scrollbar", get("chat_scroll"));
            Replace(chatBody, "input", get("chat_input"));
        }
        std::string doc = hudTemplate.empty() ? DefaultHudTemplate() : hudTemplate;
        Replace(doc, "w", Num(guiW));
        Replace(doc, "h", Num(guiH));
        Replace(doc, "chat_style", chatStyle);
        Replace(doc, "rcss", extraRcss);
        Replace(doc, "chat", chatBody);
        Replace(doc, "hud", writer.Body());
        static const char* kEngineCss =
            "\nmcimg, mcitem { display: block; position: absolute; }\n"
            ".t { display: block; position: absolute; font-family: minecraft; white-space: nowrap; overflow: visible; }\n"
            ".t.s { font-family: minecraft-shadow; }\n.t.o { font-family: minecraft-outline; }\n"
            ".link { display: block; position: absolute; }\n"
            ".link .ul { display: none; position: absolute; left: 0px; bottom: 0px; width: 100%; height: 1px; background-color: #ffff55; }\n"
            ".link:hover .ul { display: block; }\n"
            ".bt .hv { display: none; }\n.bt:hover .hv { display: block; }\n.bt:hover .df { display: none; }\n";
        if (const size_t at = doc.find("<style>"); at != std::string::npos)
            doc.insert(at + 7, kEngineCss);
        return doc;
    }
}
