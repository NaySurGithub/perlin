#include "client/ui/FormScreen.hpp"

#include "client/ui/FormatCodes.hpp"
#include "client/ui/Json.hpp"
#include "client/ui/HudData.hpp"
#include "client/ui/McFont.hpp"
#include "client/ui/RmlEmit.hpp"
#include "client/ui/UiAssets.hpp"
#include "util/Paths.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iostream>

namespace perlin::ui {
    namespace {
        std::string Str(const Json* j) {
            return j && j->IsString() ? j->string : std::string();
        }

        std::string Num(double v) {
            char buf[48];
            if (v == std::floor(v) && std::fabs(v) < 1e12)
                std::snprintf(buf, sizeof(buf), "%.1f", v);
            else
                std::snprintf(buf, sizeof(buf), "%.6g", v);
            return buf;
        }

        std::string JsonString(const std::string& s) {
            Json j(s);
            return j.Dump();
        }

        std::string F(float v) {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%.1f", static_cast<double>(v));
            return buf;
        }

        int LineCount(const std::string& text, int perLine) {
            int lines = 0;
            size_t start = 0;
            while (start <= text.size()) {
                size_t end = text.find('\n', start);
                if (end == std::string::npos)
                    end = text.size();
                const std::string line = StripFormatCodes(text.substr(start, end - start));
                lines += std::max(1, static_cast<int>((line.size() + perLine - 1) / std::max(1, perLine)));
                start = end + 1;
            }
            return lines;
        }

        std::string Rich(const std::string& text) {
            std::string out = FormatCodesToRml(text);
            std::string result;
            for (size_t i = 0; i < out.size(); i++) {
                if (out[i] == '\n')
                    result += "<br/>";
                else
                    result += out[i];
            }
            return result;
        }

        const char* kStyle =
            "body { position: absolute; left: 0px; top: 0px; margin: 0; padding: 0; background: transparent; }\n"
            "div { display: block; position: absolute; }\n"
            "#bg { left: 0px; top: 0px; background-color: rgba(0,0,0,150); z-index: 1; }\n"
            "#panel { background-color: rgba(16,16,16,235); z-index: 2; }\n"
            "#clip { overflow: hidden; z-index: 3; }\n"
            ".t { font-family: minecraft-shadow; white-space: normal; color: #ffffff; font-size: 8px; line-height: 9px; }\n"
            ".c { text-align: center; }\n"
            ".btn { background-color: #000000; z-index: 5; }\n"
            ".btn div { display: block; position: absolute; }\n"
            ".btn .rim { left: 1px; top: 1px; background-color: #aaaaaa; }\n"
            ".btn .face { left: 2px; top: 2px; background-color: #6f6f6f; }\n"
            ".btn .shade { left: 1px; background-color: #565656; }\n"
            ".btn .edge { top: 1px; background-color: #565656; }\n"
            ".btn .lbl { font-family: minecraft-shadow; left: 0px; font-size: 8px; line-height: 9px; text-align: center; color: #ffffff; white-space: nowrap; overflow: hidden; }\n"
            ".btn:hover { background-color: #ffffff; }\n"
            ".btn:hover .rim { background-color: #afafaf; }\n"
            ".btn:hover .face { background-color: #757575; }\n"
            ".field { background-color: #ffffff; z-index: 5; }\n"
            ".field .in { left: 1px; top: 1px; background-color: #000000; }\n"
            ".field.focus .in { background-color: #101030; }\n"
            ".field .txt { font-family: minecraft-shadow; left: 4px; top: 5px; font-size: 8px; line-height: 9px; white-space: nowrap; overflow: hidden; color: #e0e0e0; }\n";

        std::string Button(const std::string& id, const std::string& label, float x, float y, float w, float h) {
            const std::string W = F(w), H = F(h);
            std::string s = "<div id=\"" + EscapeRml(id) + "\" class=\"btn\" style=\"left:" + F(x) + "px;top:" + F(y) + "px;width:" + W + "px;height:" + H + "px;\">";
            s += "<div class=\"rim\" style=\"width:" + F(w - 3) + "px;height:" + F(h - 4) + "px;\"></div>";
            s += "<div class=\"face\" style=\"width:" + F(w - 4) + "px;height:" + F(h - 5) + "px;\"></div>";
            s += "<div class=\"shade\" style=\"top:" + F(h - 3) + "px;width:" + F(w - 2) + "px;height:2px;\"></div>";
            s += "<div class=\"edge\" style=\"left:" + F(w - 2) + "px;width:1px;height:" + F(h - 4) + "px;\"></div>";
            s += "<div class=\"lbl\" style=\"top:" + F((h - 9) / 2) + "px;width:" + W + "px;height:9px;\">" + Rich(label) + "</div></div>\n";
            return s;
        }
    }

    bool ParseForm(uint32_t id, const std::string& json, FormModel& out) {
        Json root;
        std::string error;
        if (!Json::Parse(json, root, error) || !root.IsObject())
            return false;
        out = FormModel{};
        out.id = id;
        out.title = Str(root.Find("title"));
        const std::string type = Str(root.Find("type"));
        if (type == "modal") {
            out.type = FormModel::Type::Modal;
            out.content = Str(root.Find("content"));
            out.button1 = Str(root.Find("button1"));
            out.button2 = Str(root.Find("button2"));
            return true;
        }
        if (type == "custom_form") {
            out.type = FormModel::Type::Custom;
            out.submit = Str(root.Find("submit"));
            const Json* content = root.Find("content");
            if (content && content->IsArray()) {
                for (const Json& e : content->array) {
                    if (!e.IsObject())
                        continue;
                    FormElement el;
                    const std::string kind = Str(e.Find("type"));
                    el.text = Str(e.Find("text"));
                    if (kind == "label") {
                        el.kind = FormElement::Kind::Label;
                    } else if (kind == "header") {
                        el.kind = FormElement::Kind::Header;
                    } else if (kind == "divider") {
                        el.kind = FormElement::Kind::Divider;
                    } else if (kind == "input") {
                        el.kind = FormElement::Kind::Input;
                        el.placeholder = Str(e.Find("placeholder"));
                        el.value = Str(e.Find("default"));
                    } else if (kind == "toggle") {
                        el.kind = FormElement::Kind::Toggle;
                        if (const Json* d = e.Find("default"))
                            el.flag = d->BoolOr(false);
                    } else if (kind == "slider") {
                        el.kind = FormElement::Kind::Slider;
                        el.min = e.Find("min") ? e.Find("min")->NumberOr(0) : 0;
                        el.max = e.Find("max") ? e.Find("max")->NumberOr(1) : 1;
                        el.step = e.Find("step") ? e.Find("step")->NumberOr(1) : 1;
                        if (el.step <= 0)
                            el.step = 1;
                        el.number = e.Find("default") ? e.Find("default")->NumberOr(el.min) : el.min;
                    } else if (kind == "step_slider" || kind == "dropdown") {
                        el.kind = kind == "dropdown" ? FormElement::Kind::Dropdown : FormElement::Kind::StepSlider;
                        const Json* list = e.Find(kind == "dropdown" ? "options" : "steps");
                        if (list && list->IsArray())
                            for (const Json& o : list->array)
                                el.options.push_back(o.IsString() ? o.string : std::string());
                        if (const Json* d = e.Find("default"))
                            el.index = static_cast<int>(d->NumberOr(0));
                        if (el.options.empty())
                            el.options.push_back("");
                        el.index = std::clamp(el.index, 0, static_cast<int>(el.options.size()) - 1);
                    } else {
                        continue;
                    }
                    out.elements.push_back(std::move(el));
                }
            }
            return true;
        }
        out.type = FormModel::Type::Simple;
        out.content = Str(root.Find("content"));
        const Json* buttons = root.Find("buttons");
        if (buttons && buttons->IsArray())
            for (const Json& b : buttons->array) {
                FormButton fb;
                fb.text = Str(b.Find("text"));
                fb.kind = Str(b.Find("type"));
                if (const Json* img = b.Find("image")) {
                    fb.imageType = Str(img->Find("type"));
                    fb.imageData = Str(img->Find("data"));
                }
                out.buttons.push_back(std::move(fb));
            }
        return true;
    }

    std::string BuildFormRml(const FormModel& form, int guiW, int guiH, float scroll, float& contentHeight, float& viewHeight) {
        const float panelW = static_cast<float>(std::min(guiW - 16, 260));
        const float inner = panelW - 20;
        const int perLine = std::max(8, static_cast<int>(inner / 5.2f));
        const float panelX = (guiW - panelW) / 2.0f;
        const float viewTop = 30.0f;
        viewHeight = static_cast<float>(std::max(60, guiH - 60));
        std::string body;
        float y = 0.0f;
        const auto text = [&](const std::string& t, bool center) {
            const int lines = LineCount(t, perLine);
            const float h = static_cast<float>(lines * 9);
            body += "<div class=\"t" + std::string(center ? " c" : "") + "\" style=\"left:10px;top:" + F(y) + "px;width:" + F(inner) + "px;height:" + F(h) + "px;\">" + Rich(t) + "</div>\n";
            y += h + 4.0f;
        };
        if (!form.content.empty() && form.type != FormModel::Type::Custom)
            text(form.content, false);
        if (form.type == FormModel::Type::Simple) {
            for (size_t i = 0; i < form.buttons.size(); i++) {
                body += Button("b:" + std::to_string(i), form.buttons[i].text, 10, y, inner, 20);
                y += 24.0f;
            }
        } else if (form.type == FormModel::Type::Modal) {
            const float half = (inner - 4) / 2;
            body += Button("m:1", form.button1, 10, y, half, 20);
            body += Button("m:2", form.button2, 10 + half + 4, y, half, 20);
            y += 24.0f;
        } else {
            for (size_t i = 0; i < form.elements.size(); i++) {
                const FormElement& e = form.elements[i];
                const std::string n = std::to_string(i);
                switch (e.kind) {
                    case FormElement::Kind::Label:
                        text(e.text, false);
                        break;
                    case FormElement::Kind::Header:
                        text(e.text, true);
                        break;
                    case FormElement::Kind::Divider:
                        body += "<div style=\"left:10px;top:" + F(y + 2) + "px;width:" + F(inner) + "px;height:1px;background-color:#707070;\"></div>\n";
                        y += 6.0f;
                        break;
                    case FormElement::Kind::Input: {
                        if (!e.text.empty())
                            text(e.text, false);
                        const bool focus = form.focus == static_cast<int>(i);
                        const std::string shown = e.value.empty() && !focus ? "\xC2\xA7" "8" + e.placeholder : e.value + (focus ? "_" : "");
                        body += "<div id=\"i:" + n + "\" class=\"field" + (focus ? " focus" : "") + "\" style=\"left:10px;top:" + F(y) + "px;width:" + F(inner) + "px;height:20px;\">";
                        body += "<div class=\"in\" style=\"width:" + F(inner - 2) + "px;height:18px;\"></div>";
                        body += "<div class=\"txt\" style=\"width:" + F(inner - 8) + "px;height:9px;\">" + Rich(shown) + "</div></div>\n";
                        y += 24.0f;
                        break;
                    }
                    case FormElement::Kind::Toggle:
                        body += Button("t:" + n, e.text + ": " + (e.flag ? "\xC2\xA7" "aON" : "\xC2\xA7" "cOFF"), 10, y, inner, 20);
                        y += 24.0f;
                        break;
                    case FormElement::Kind::Dropdown:
                        body += Button("d:" + n, e.text + ": " + e.options[static_cast<size_t>(e.index)], 10, y, inner, 20);
                        y += 24.0f;
                        break;
                    case FormElement::Kind::Slider:
                        body += Button("s:" + n + ":-", "<", 10, y, 20, 20);
                        body += Button("s:" + n + ":+", ">", 10 + inner - 20, y, 20, 20);
                        body += "<div class=\"t c\" style=\"left:34px;top:" + F(y + 5) + "px;width:" + F(inner - 68) + "px;height:9px;\">" + Rich(e.text + ": " + Num(e.number)) + "</div>\n";
                        y += 24.0f;
                        break;
                    case FormElement::Kind::StepSlider:
                        body += Button("s:" + n + ":-", "<", 10, y, 20, 20);
                        body += Button("s:" + n + ":+", ">", 10 + inner - 20, y, 20, 20);
                        body += "<div class=\"t c\" style=\"left:34px;top:" + F(y + 5) + "px;width:" + F(inner - 68) + "px;height:9px;\">" + Rich(e.text + ": " + e.options[static_cast<size_t>(e.index)]) + "</div>\n";
                        y += 24.0f;
                        break;
                }
            }
            body += Button("submit", form.submit.empty() ? "Submit" : form.submit, 10, y, inner, 20);
            y += 24.0f;
        }
        contentHeight = y + 6.0f;
        const float maxScroll = std::max(0.0f, contentHeight - viewHeight);
        const float s = std::clamp(scroll, 0.0f, maxScroll);
        const float visible = std::min(contentHeight, viewHeight);

        std::string out = "<rml>\n<head>\n<title>form</title>\n<style>\n";
        out += "body { width: " + std::to_string(guiW) + "px; height: " + std::to_string(guiH) + "px; }\n";
        out += kStyle;
        out += "</style>\n</head>\n<body>\n";
        out += "<div id=\"bg\" style=\"width:" + std::to_string(guiW) + "px;height:" + std::to_string(guiH) + "px;\"></div>\n";
        out += "<div id=\"panel\" style=\"left:" + F(panelX) + "px;top:" + F(viewTop - 18) + "px;width:" + F(panelW) + "px;height:" + F(visible + 24) + "px;\"></div>\n";
        out += "<div class=\"t c\" style=\"z-index:4;left:" + F(panelX) + "px;top:" + F(viewTop - 13) + "px;width:" + F(panelW) + "px;height:9px;\">" + Rich(form.title) + "</div>\n";
        out += Button(kFormCloseId, "x", panelX + panelW - 14, viewTop - 16, 12, 12);
        out += "<div id=\"clip\" style=\"left:" + F(panelX) + "px;top:" + F(viewTop) + "px;width:" + F(panelW) + "px;height:" + F(visible) + "px;\">\n";
        out += "<div style=\"left:0px;top:" + F(-s) + "px;width:" + F(panelW) + "px;height:" + F(contentHeight) + "px;\">\n" + body + "</div>\n</div>\n";
        out += "</body>\n</rml>\n";
        return out;
    }

    std::string FormCustomResponse(const FormModel& form) {
        std::string out = "[";
        for (size_t i = 0; i < form.elements.size(); i++) {
            const FormElement& e = form.elements[i];
            if (i)
                out += ",";
            switch (e.kind) {
                case FormElement::Kind::Input: out += JsonString(e.value); break;
                case FormElement::Kind::Toggle: out += e.flag ? "true" : "false"; break;
                case FormElement::Kind::Slider: out += Num(e.number); break;
                case FormElement::Kind::StepSlider:
                case FormElement::Kind::Dropdown: out += std::to_string(e.index); break;
                default: out += "null"; break;
            }
        }
        out += "]";
        return out;
    }

    bool FormClick(FormModel& form, const std::string& id, std::string& response) {
        if (id.size() < 2)
            return false;
        if (id.rfind("fb:", 0) == 0) {
            const int index = std::atoi(id.c_str() + 3);
            response = form.type == FormModel::Type::Modal ? (index == 0 ? "true" : "false") : std::to_string(index);
            return true;
        }
        const char kind = id[0];
        if (id == "submit") {
            response = FormCustomResponse(form);
            return true;
        }
        if (id[1] != ':')
            return false;
        const std::string rest = id.substr(2);
        const size_t colon = rest.find(':');
        const int index = std::atoi(rest.substr(0, colon).c_str());
        if (kind == 'b') {
            response = std::to_string(index);
            return true;
        }
        if (kind == 'm') {
            response = index == 1 ? "true" : "false";
            return true;
        }
        if (index < 0 || static_cast<size_t>(index) >= form.elements.size())
            return false;
        FormElement& e = form.elements[static_cast<size_t>(index)];
        if (kind == 'i') {
            form.focus = index;
        } else if (kind == 't') {
            e.flag = !e.flag;
        } else if (kind == 'd') {
            e.index = (e.index + 1) % static_cast<int>(e.options.size());
        } else if (kind == 's') {
            const bool plus = colon != std::string::npos && rest[colon + 1] == '+';
            if (e.kind == FormElement::Kind::Slider)
                e.number = std::clamp(e.number + (plus ? e.step : -e.step), e.min, e.max);
            else
                e.index = std::clamp(e.index + (plus ? 1 : -1), 0, static_cast<int>(e.options.size()) - 1);
        }
        return false;
    }

    void FormTypeChar(FormModel& form, int cp) {
        if (form.focus < 0 || static_cast<size_t>(form.focus) >= form.elements.size() || cp < 32)
            return;
        FormElement& e = form.elements[static_cast<size_t>(form.focus)];
        if (e.kind != FormElement::Kind::Input || e.value.size() > 500)
            return;
        if (cp < 0x80) {
            e.value += static_cast<char>(cp);
        } else if (cp < 0x800) {
            e.value += static_cast<char>(0xC0 | (cp >> 6));
            e.value += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            e.value += static_cast<char>(0xE0 | (cp >> 12));
            e.value += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            e.value += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }

    void FormBackspace(FormModel& form) {
        if (form.focus < 0 || static_cast<size_t>(form.focus) >= form.elements.size())
            return;
        std::string& v = form.elements[static_cast<size_t>(form.focus)].value;
        while (!v.empty() && (static_cast<unsigned char>(v.back()) & 0xC0) == 0x80)
            v.pop_back();
        if (!v.empty())
            v.pop_back();
    }

    namespace {
        class FormData final : public UiData {
        public:
            explicit FormData(const FormModel& f) : form(f) {
                if (form.type == FormModel::Type::Modal) {
                    FormButton a, b;
                    a.text = form.button1;
                    b.text = form.button2;
                    items.push_back(a);
                    items.push_back(b);
                } else {
                    items = form.buttons;
                }
            }

            bool Global(const std::string& name, Json& out) const override {
                if (name == "#title_text") {
                    out = Json(form.title);
                    return true;
                }
                if (name == "#form_text") {
                    out = Json(form.content);
                    return true;
                }
                return false;
            }

            int CollectionSize(const std::string& collection) const override {
                return collection == "form_buttons" ? static_cast<int>(items.size()) : 0;
            }

            bool CollectionValue(const std::string& collection, int index, const std::string& binding, Json& out) const override {
                if (collection != "form_buttons" || index < 0 || static_cast<size_t>(index) >= items.size())
                    return false;
                const FormButton& b = items[static_cast<size_t>(index)];
                if (binding == "#form_button_text") {
                    out = Json(b.text);
                    return true;
                }
                if (binding == "#form_button_texture") {
                    out = Json(b.imageType == "path" ? b.imageData : std::string());
                    return true;
                }
                if (binding == "#form_button_texture_file_system") {
                    out = Json(std::string());
                    return true;
                }
                if (binding == "#form_button_contents") {
                    out = Json(static_cast<double>(items.size()));
                    return true;
                }
                return false;
            }

            std::vector<FactoryItem> Factory(const std::string& name) const override {
                std::vector<FactoryItem> result;
                if (name == "server_form_factory") {
                    result.push_back({"long_form", {}, {}, -1});
                } else if (name == "buttons") {
                    for (size_t i = 0; i < items.size(); i++) {
                        const std::string& k = items[i].kind;
                        const std::string id = k == "label" || k == "header" || k == "divider" ? k : "button";
                        result.push_back({id, {}, "form_buttons", static_cast<int>(i)});
                    }
                }
                return result;
            }

        private:
            const FormModel& form;
            std::vector<FormButton> items;
        };

        const char* kFormTemplate =
            "<rml>\n<head>\n<title>form</title>\n<style>\n"
            "body { position: absolute; left: 0px; top: 0px; width: {{w}}px; height: {{h}}px; margin: 0; padding: 0; background: transparent; }\n"
            "mcimg, mcitem { display: block; position: absolute; }\n"
            "{{chat_style}}{{rcss}}\n</style>\n</head>\n<body>\n{{chat}}{{hud}}</body>\n</rml>\n";
    }

    FormUi::FormUi() {
        services.textWidth = [](const std::string& text, float s) { return McFont::Instance().Width(text, s); };
        services.lineHeight = [](float s) { return static_cast<float>(McFont::kLineHeight) * s; };
        services.textureSize = [](const std::string& tex, int& w, int& h) { return UiAssets::Instance().ImageSize(tex, w, h); };
        services.localize = [](const std::string& key) { return UiAssets::Instance().Localize(key); };
        emitter.SetTemplates(kFormTemplate, std::string());
    }

    bool FormUi::Prepare(const std::vector<std::string>& packDirs) {
        std::vector<std::string> roots;
        for (size_t i = packDirs.size(); i-- > 0;)
            roots.push_back(packDirs[i] + "/ui");
        const std::string data = paths::DataDir();
        roots.push_back(data + "/ui");
        roots.push_back(data + "/ui_defaults");
        roots.push_back(UiAssets::Instance().VanillaRoot() + "/ui");
        library.SetRoots(roots);
        tree.reset();
        problem.clear();
        if (!library.Scan()) {
            problem = "no ui folder";
            return false;
        }
        BuildReport report;
        tree = BuildTree(library, "server_form.third_party_server_screen", EngineRootVariables(), report);
        if (!tree) {
            problem = "server_form.third_party_server_screen could not be built";
            return false;
        }
        for (const std::string& missing : report.missingControls)
            std::cout << "[form] missing control " << missing << std::endl;
        return true;
    }

    bool FormUi::Build(const FormModel& form, int guiW, int guiH, float scroll, std::string& rml, float& contentHeight, float& viewHeight) {
        if (!tree || form.type == FormModel::Type::Custom)
            return false;
        FormData data(form);
        const std::unique_ptr<LNode> root = EvaluateTree(*tree, data, services, static_cast<float>(guiW), static_cast<float>(guiH));
        if (!root)
            return false;
        emitter.SetScroll(scroll);
        rml = emitter.Emit(*root, static_cast<float>(guiW), static_cast<float>(guiH), std::string(), false);
        contentHeight = emitter.ContentHeight();
        viewHeight = emitter.ViewportHeight();
        const size_t buttons = form.type == FormModel::Type::Modal ? 2 : form.buttons.size();
        if (buttons > 0 && rml.find("id=\"fb:0\"") == std::string::npos) {
            problem = "form buttons were not produced by the ui files";
            return false;
        }
        return true;
    }
}
