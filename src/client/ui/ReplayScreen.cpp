#include "client/ui/ReplayScreen.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "client/ui/RmlEmit.hpp"

namespace perlin::ui {
    namespace {
        std::string N(int v) { return std::to_string(v); }
        std::string F(float v) { return std::to_string(static_cast<int>(std::lround(v))); }

        std::string Hex(unsigned rgb) {
            char buf[16];
            std::snprintf(buf, sizeof buf, "#%06x", rgb & 0xFFFFFFu);
            return buf;
        }

        int TextWidth(const std::string& text) {
            int n = 0;
            for (unsigned char c : text) {
                if ((c & 0xC0) == 0x80)
                    continue;
                if (c == 'i' || c == '!' || c == '.' || c == ',' || c == ':' || c == ';' || c == '|' || c == '\'')
                    n += 2;
                else if (c == 'l' || c == '`')
                    n += 3;
                else if (c == 'I' || c == 't' || c == '[' || c == ']' || c == ' ')
                    n += 4;
                else if (c == 'f' || c == 'k' || c == '<' || c == '>' || c == '{' || c == '}' || c == '(' || c == ')' || c == '*' || c == '"')
                    n += 5;
                else
                    n += 6;
            }
            return n;
        }

        const char* kStyle = R"(<rml>
<head>
<title>replay</title>
<style>
body { position: absolute; left: 0px; top: 0px; margin: 0; padding: 0; background: transparent; }
div { display: block; position: absolute; }
.btn { height: 20px; background-color: #000000; z-index: 5; }
.btn div { position: absolute; }
.btn .rim { left: 1px; top: 1px; height: 16px; background-color: #aaaaaa; }
.btn .face { left: 2px; top: 2px; height: 15px; background-color: #6f6f6f; }
.btn .shade { left: 1px; top: 17px; height: 2px; background-color: #565656; }
.btn .edge { top: 1px; width: 1px; height: 16px; background-color: #565656; }
.btn .lbl { font-family: minecraft-shadow; left: 0px; top: 6px; height: 9px; font-size: 8px; line-height: 9px; text-align: center; color: #ffffff; white-space: nowrap; }
.btn:hover { background-color: #ffffff; }
.btn:hover .rim { background-color: #afafaf; }
.btn:hover .face { background-color: #757575; }
.btn:hover .shade, .btn:hover .edge { background-color: #5c5c5c; }
.btn.on .face { background-color: #4c7a2f; }
.btn.on .rim { background-color: #7fb85a; }
.btn.off, .btn.off:hover { background-color: #000000; }
.btn.off .rim, .btn.off .face, .btn.off .shade, .btn.off .edge { background-color: #2c2c2c; }
.btn.off .lbl { color: #a0a0a0; }
.txt { font-family: minecraft-shadow; font-size: 8px; line-height: 9px; color: #ffffff; white-space: nowrap; z-index: 6; }
.gray { color: #aaaaaa; }
.yellow { color: #ffff55; }
.green { color: #55ff55; }
</style>
</head>
<body>
)";

        std::string Button(const std::string& id, float x, float y, float w, const std::string& label, bool on = false, bool off = false) {
            std::string d = "<div id=\"" + id + "\" class=\"btn" + (on ? " on" : "") + (off ? " off" : "") + "\" style=\"left: " + F(x) + "px; top: " + F(y) + "px; width: " + F(w) + "px;\">";
            d += "<div class=\"rim\" style=\"width: " + F(w - 3) + "px;\"></div><div class=\"face\" style=\"width: " + F(w - 4) + "px;\"></div>";
            d += "<div class=\"shade\" style=\"width: " + F(w - 2) + "px;\"></div><div class=\"edge\" style=\"left: " + F(w - 2) + "px;\"></div>";
            d += "<div class=\"lbl\" style=\"width: " + F(w) + "px;\">" + EscapeRml(label) + "</div></div>\n";
            return d;
        }

        std::string Rect(float x, float y, float w, float h, const std::string& color, int z = 6) {
            return "<div style=\"left: " + F(x) + "px; top: " + F(y) + "px; width: " + F(std::max(w, 0.0f)) + "px; height: " + F(std::max(h, 0.0f)) +
                   "px; background-color: " + color + "; z-index: " + N(z) + ";\"></div>\n";
        }

        std::string Text(float x, float y, float w, const std::string& text, const std::string& cls = "", const std::string& align = "left") {
            return "<div class=\"txt " + cls + "\" style=\"left: " + F(x) + "px; top: " + F(y) + "px; width: " + F(w) + "px; height: 9px; text-align: " + align + ";\">" +
                   EscapeRml(text) + "</div>\n";
        }
    }

    std::string FormatReplayTime(double ms) {
        if (ms < 0)
            ms = 0;
        const long long total = static_cast<long long>(ms / 1000.0);
        char buf[48];
        if (total >= 3600)
            std::snprintf(buf, sizeof buf, "%lld:%02lld:%02lld", total / 3600, (total / 60) % 60, total % 60);
        else
            std::snprintf(buf, sizeof buf, "%lld:%02lld", total / 60, total % 60);
        return buf;
    }

    ReplayBarLayout ComputeReplayBarLayout(int guiW, int guiH) {
        ReplayBarLayout l;
        l.h = 62;
        l.w = static_cast<float>(guiW);
        l.x = 0;
        l.y = static_cast<float>(guiH) - l.h;
        l.trackX = 40;
        l.trackW = static_cast<float>(guiW) - 80;
        l.trackY = l.y + 17;
        l.trackH = 10;
        return l;
    }

    std::string BuildReplayBarRml(const ReplayBarModel& m, int guiW, int guiH) {
        std::string d = kStyle;
        const size_t body = d.find("<body>");
        d.insert(body + 6, "");
        d.replace(body, 6, "<body style=\"width: " + N(guiW) + "px; height: " + N(guiH) + "px;\">");
        const ReplayBarLayout l = ComputeReplayBarLayout(guiW, guiH);
        const std::string shown = FormatReplayTime(m.scrubMs >= 0 ? m.scrubMs : m.timeMs) + " / " + FormatReplayTime(m.durationMs);
        if (m.hudShown) {
            d += "</body>\n</rml>\n";
            return d;
        }
        if (!m.open) {
            std::string line = std::string(m.paused ? "Paused  " : "") + shown + (m.catching ? "  (seeking...)" : "") + "   Esc: replay controls";
            if (!m.status.empty())
                line = m.status + "   " + line;
            d += Text(4, static_cast<float>(guiH) - 11, static_cast<float>(guiW) - 8, line, "gray");
            d += "</body>\n</rml>\n";
            return d;
        }
        d += Rect(l.x, l.y, l.w, l.h, "#000000b4", 3);
        d += Rect(l.x, l.y, l.w, 1, "#ffffff55", 4);
        d += Text(6, l.y + 5, l.w / 2, m.title.empty() ? "Replay" : m.title, "yellow");
        d += Text(l.w / 2, l.y + 5, l.w / 2 - 6, m.status.empty() ? shown : m.status + "   " + shown, "", "right");
        // timeline
        d += Rect(l.trackX - 1, l.trackY - 1, l.trackW + 2, l.trackH + 2, "#000000", 5);
        d += Rect(l.trackX, l.trackY, l.trackW, l.trackH, "#3a3a3a", 5);
        const double dur = std::max(1.0, m.durationMs);
        const auto xAt = [&](double t) { return l.trackX + static_cast<float>(std::clamp(t / dur, 0.0, 1.0)) * l.trackW; };
        if (m.inMs >= 0 || m.outMs >= 0) {
            const float a = xAt(m.inMs >= 0 ? m.inMs : 0.0), b = xAt(m.outMs >= 0 ? m.outMs : dur);
            d += Rect(a, l.trackY, b - a, l.trackH, "#3d5a7a", 5);
        }
        const double cur = m.scrubMs >= 0 ? m.scrubMs : m.timeMs;
        d += Rect(l.trackX, l.trackY, xAt(cur) - l.trackX, l.trackH, "#6b8f4e", 5);
        for (const auto& k : m.keys)
            d += Rect(xAt(k.timeMs) - 2, l.trackY + 3, 5, 5, k.track == 0 ? "#ffd84d" : (k.track == 1 ? "#4dc3ff" : "#ff7a4d"), 7);
        for (const auto& mk : m.markers)
            d += Rect(xAt(mk.timeMs) - 1, l.trackY - 3, 3, l.trackH + 6, Hex(mk.color), 7);
        d += Rect(xAt(cur) - 1, l.trackY - 3, 3, l.trackH + 6, "#ffffff", 8);
        d += Text(4, l.trackY + 1, 34, FormatReplayTime(cur), "gray");
        d += Text(l.trackX + l.trackW + 4, l.trackY + 1, 34, FormatReplayTime(m.durationMs), "gray");
        // clickable strip over the whole track (taller than the bar for easy grabbing)
        d += "<div id=\"" + std::string(kRbTrack) + "\" style=\"left: " + F(l.trackX - 3) + "px; top: " + F(l.trackY - 5) + "px; width: " + F(l.trackW + 6) + "px; height: " + F(l.trackH + 10) + "px; z-index: 9;\"></div>\n";

        struct B {
            const char* id;
            std::string label;
            bool on = false;
        };
        char speedBuf[32];
        if (m.speed >= 1.0 && std::fabs(m.speed - std::round(m.speed)) < 0.001)
            std::snprintf(speedBuf, sizeof speedBuf, "%.0fx", m.speed);
        else
            std::snprintf(speedBuf, sizeof speedBuf, "%gx", m.speed);
        const char* camLabel = m.cameraMode == 0 ? "Cam: Free" : (m.cameraMode == 1 ? "Cam: Player" : "Cam: Path");
        const std::vector<B> row = {
            {kRbBack, "-5s"}, {kRbPlay, m.paused || m.ended ? "Play" : "Pause"}, {kRbFwd, "+5s"},
            {kRbSlower, "-"}, {kRbSpeed, speedBuf}, {kRbFaster, "+"},
            {kRbCamera, camLabel}, {kRbMarker, "Marker"}, {kRbKeyframes, "Keyframes"},
            {kRbHud, m.hideHud ? "HUD: Off" : "HUD: On", !m.hideHud}, {kRbExport, "Export"}, {kRbOptions, "Options"}, {kRbExit, "Exit"},
        };
        std::vector<float> widths;
        float total = 0;
        for (const B& b : row) {
            const float w = static_cast<float>(std::max(20, TextWidth(b.label) + 12));
            widths.push_back(w);
            total += w;
        }
        const float gap = 3;
        total += gap * static_cast<float>(row.size() - 1);
        const float avail = static_cast<float>(guiW) - 8;
        const float scale = total > avail ? (avail - gap * static_cast<float>(row.size() - 1)) / (total - gap * static_cast<float>(row.size() - 1)) : 1.0f;
        float x = std::max(4.0f, (static_cast<float>(guiW) - std::min(total, avail)) / 2.0f);
        const float y = l.y + 36;
        for (size_t i = 0; i < row.size(); i++) {
            const float w = std::floor(widths[i] * scale);
            d += Button(row[i].id, x, y, w, row[i].label, row[i].on);
            x += w + gap;
        }
        d += "</body>\n</rml>\n";
        return d;
    }

    PanelLayout ComputePanelLayout(const PanelModel& m, int guiW, int guiH) {
        PanelLayout l;
        l.w = static_cast<float>(std::min(guiW - 12, 340));
        l.x = (static_cast<float>(guiW) - l.w) / 2;
        l.y = 4;
        l.h = static_cast<float>(guiH) - 8;
        l.rowsTop = l.y + 14 + (m.tabs.empty() ? 0 : 24);
        const float footer = l.y + l.h - 24 - (m.progress >= 0 ? 12 : 0);
        l.visibleRows = std::max(1, static_cast<int>((footer - 4 - l.rowsTop) / l.rowH));
        return l;
    }

    std::string BuildPanelRml(const PanelModel& m, int guiW, int guiH) {
        std::string d = kStyle;
        const size_t body = d.find("<body>");
        d.replace(body, 6, "<body style=\"width: " + N(guiW) + "px; height: " + N(guiH) + "px;\">");
        const PanelLayout l = ComputePanelLayout(m, guiW, guiH);
        d += Rect(l.x, l.y, l.w, l.h, "#000000d0", 1);
        d += Rect(l.x, l.y, l.w, 1, "#ffffff66", 2);
        d += Rect(l.x, l.y + l.h - 1, l.w, 1, "#ffffff66", 2);
        d += Text(l.x + 6, l.y + 4, l.w - 12, m.title, "yellow");
        if (!m.status.empty())
            d += Text(l.x + 6, l.y + 4, l.w - 12, m.status, "green", "right");
        if (!m.tabs.empty()) {
            const float tw = (l.w - 8) / static_cast<float>(m.tabs.size());
            for (size_t i = 0; i < m.tabs.size(); i++)
                d += Button(std::string(kPanelTabPrefix) + N(static_cast<int>(i)), l.x + 4 + tw * static_cast<float>(i), l.y + 16, tw - 2, m.tabs[i], static_cast<int>(i) == m.tab);
        }
        const float labelW = l.w * 0.38f;
        const float wx = l.x + 6 + labelW;
        const float ww = l.w - 12 - labelW;
        float y = l.rowsTop;
        const int total = static_cast<int>(m.rows.size());
        for (int i = m.scroll; i < total && i < m.scroll + l.visibleRows; i++, y += l.rowH) {
            const PanelRow& r = m.rows[static_cast<size_t>(i)];
            const std::string cls = r.enabled ? "" : "gray";
            switch (r.kind) {
            case PanelRow::Kind::Header:
                d += Text(l.x + 6, y + 8, l.w - 12, r.label, "yellow");
                d += Rect(l.x + 6, y + 19, l.w - 12, 1, "#ffffff33", 2);
                break;
            case PanelRow::Kind::Info:
                d += Text(l.x + 6, y + 6, l.w - 12, r.label + (r.value.empty() ? "" : ": " + r.value), "gray");
                break;
            case PanelRow::Kind::Cycle:
                d += Text(l.x + 6, y + 6, labelW, r.label, cls);
                d += Button(r.id + ":-", wx, y + 1, 16, "<", false, !r.enabled);
                d += Button(r.id + ":+", wx + 18, y + 1, ww - 36, r.value, false, !r.enabled);
                d += Button(r.id + ":n", wx + ww - 16, y + 1, 16, ">", false, !r.enabled);
                break;
            case PanelRow::Kind::Toggle:
                d += Text(l.x + 6, y + 6, labelW, r.label, cls);
                d += Button(r.id, wx, y + 1, ww, r.on ? "On" : "Off", r.on, !r.enabled);
                break;
            case PanelRow::Kind::Button:
                d += Button(r.id, l.x + 6, y + 1, l.w - 12, r.label, false, !r.enabled);
                break;
            case PanelRow::Kind::Field: {
                d += Text(l.x + 6, y + 6, labelW, r.label, cls);
                d += "<div id=\"" + r.id + "\" style=\"left: " + F(wx) + "px; top: " + F(y + 1) + "px; width: " + F(ww) + "px; height: 20px; background-color: " +
                     (r.focused ? "#ffffff" : "#666666") + "; z-index: 5;\"></div>\n";
                d += Rect(wx + 1, y + 2, ww - 2, 18, "#000000", 6);
                // keep the tail of long values visible
                std::string shown = r.value;
                const int maxPx = static_cast<int>(ww) - 8;
                while (!shown.empty() && TextWidth(shown) > maxPx) {
                    size_t cut = 1;
                    while (cut < shown.size() && (static_cast<unsigned char>(shown[cut]) & 0xC0) == 0x80) cut++;
                    shown.erase(0, cut);
                }
                d += Text(wx + 4, y + 7, ww - 6, shown + (r.focused && m.caret ? "_" : ""), r.enabled ? "" : "gray");
                break;
            }
            }
        }
        if (total > l.visibleRows) {
            const float trackH = l.visibleRows * l.rowH;
            const float thumbH = std::max(8.0f, trackH * static_cast<float>(l.visibleRows) / static_cast<float>(total));
            const float maxScroll = static_cast<float>(total - l.visibleRows);
            const float thumbY = l.rowsTop + (trackH - thumbH) * (static_cast<float>(m.scroll) / maxScroll);
            d += Rect(l.x + l.w - 4, l.rowsTop, 2, trackH, "#333333", 3);
            d += Rect(l.x + l.w - 4, thumbY, 2, thumbH, "#aaaaaa", 4);
        }
        float by = l.y + l.h - 24;
        if (m.progress >= 0) {
            d += Rect(l.x + 6, by - 10, l.w - 12, 6, "#333333", 3);
            d += Rect(l.x + 6, by - 10, (l.w - 12) * std::min(1.0f, m.progress), 6, "#55ff55", 4);
        }
        if (!m.buttons.empty()) {
            const float gap = 4;
            const float bw = (l.w - 12 - gap * static_cast<float>(m.buttons.size() - 1)) / static_cast<float>(m.buttons.size());
            for (size_t i = 0; i < m.buttons.size(); i++)
                d += Button(m.buttons[i].id, l.x + 6 + (bw + gap) * static_cast<float>(i), by, bw, m.buttons[i].label, false, !m.buttons[i].enabled);
        }
        d += "</body>\n</rml>\n";
        return d;
    }
}
