// LoadUiTemplate: built-in template changes must reach the on-disk copy without deleting the ui
// folder, while live edits to the copy still apply until the built-in changes again.
#include "client/ui/UiTemplate.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace fs = std::filesystem;
using perlin::ui::LoadUiTemplate;

static int failures = 0;
#define CHECK(c)                                                                                                       \
    do {                                                                                                               \
        if (!(c)) {                                                                                                    \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c);                                                   \
            failures++;                                                                                                \
        }                                                                                                              \
    } while (0)

static std::string Slurp(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

static void Spit(const fs::path& p, const std::string& s) {
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    out << s;
}

int main() {
    const fs::path dir = fs::temp_directory_path() / "perlin_uitemplate_selftest";
    fs::remove_all(dir);
    const fs::path file = dir / "ui" / "x.rml";
    uint64_t v = 0;

    // First run writes the built-in copy and returns it.
    CHECK(LoadUiTemplate(dir.string(), "x.rml", "<p>v1</p>", v) == "<p>v1</p>");
    CHECK(fs::exists(file));
    CHECK(v != 0);

    // Unchanged built-in: same content back.
    CHECK(LoadUiTemplate(dir.string(), "x.rml", "<p>v1</p>", v) == "<p>v1</p>");

    // Live edit on disk (header kept) is used while the built-in is unchanged.
    std::string onDisk = Slurp(file);
    onDisk.replace(onDisk.find("v1"), 2, "EDIT");
    Spit(file, onDisk);
    CHECK(LoadUiTemplate(dir.string(), "x.rml", "<p>v1</p>", v) == "<p>EDIT</p>");

    // New build changes the built-in: refreshed automatically, edited copy backed up.
    CHECK(LoadUiTemplate(dir.string(), "x.rml", "<p>v2</p>", v) == "<p>v2</p>");
    CHECK(fs::exists(dir / "ui" / "x.rml.old"));
    CHECK(Slurp(dir / "ui" / "x.rml.old").find("EDIT") != std::string::npos);

    // Unedited copy + another change: refreshed, no backup needed.
    fs::remove(dir / "ui" / "x.rml.old");
    CHECK(LoadUiTemplate(dir.string(), "x.rml", "<p>v3</p>", v) == "<p>v3</p>");
    CHECK(!fs::exists(dir / "ui" / "x.rml.old"));

    // Legacy file from an older build (no header): replaced by the current built-in.
    Spit(file, "<p>legacy</p>");
    CHECK(LoadUiTemplate(dir.string(), "x.rml", "<p>v3</p>", v) == "<p>v3</p>");
    CHECK(fs::exists(dir / "ui" / "x.rml.old"));

    fs::remove_all(dir);
    if (failures == 0)
        std::printf("uitemplate_selftest: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
