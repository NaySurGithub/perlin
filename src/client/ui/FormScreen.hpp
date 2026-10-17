#pragma once
#include <cstdint>
#include <string>
#include <memory>
#include <vector>

#include "client/ui/JsonUi.hpp"
#include "client/ui/RmlEmit.hpp"
#include "client/ui/UiLayout.hpp"

namespace perlin::ui {
    struct FormButton {
        std::string text;
        std::string imageType, imageData, kind;
    };

    struct FormElement {
        enum class Kind { Label, Header, Divider, Input, Toggle, Slider, StepSlider, Dropdown };
        Kind kind = Kind::Label;
        std::string text, placeholder, value;
        bool flag = false;
        double number = 0, min = 0, max = 1, step = 1;
        int index = 0;
        std::vector<std::string> options;
    };

    struct FormModel {
        enum class Type { Simple, Modal, Custom };
        uint32_t id = 0;
        Type type = Type::Simple;
        std::string title, content, button1, button2, submit;
        std::vector<FormButton> buttons;
        std::vector<FormElement> elements;
        int focus = -1;
    };

    inline constexpr const char* kFormCloseId = "close";

    class FormUi {
    public:
        FormUi();
        bool Prepare(const std::vector<std::string>& packDirs);
        bool Build(const FormModel& form, int guiW, int guiH, float scroll, std::string& rml, float& contentHeight, float& viewHeight);
        const std::string& Problem() const { return problem; }

    private:
        UiLibrary library;
        std::unique_ptr<Node> tree;
        RmlEmitter emitter;
        UiServices services;
        std::string problem;
    };

    bool ParseForm(uint32_t id, const std::string& json, FormModel& out);
    std::string BuildFormRml(const FormModel& form, int guiW, int guiH, float scroll, float& contentHeight, float& viewHeight);
    bool FormClick(FormModel& form, const std::string& id, std::string& response);
    std::string FormCustomResponse(const FormModel& form);
    void FormTypeChar(FormModel& form, int codepoint);
    void FormBackspace(FormModel& form);
}
