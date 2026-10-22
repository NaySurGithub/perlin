#pragma once
#include "client/javashaders/Include.hpp"
#include "client/javashaders/PackSource.hpp"
#include "client/javashaders/Properties.hpp"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace perlin::jshader {
    enum class Stage { Vertex, Geometry, Fragment, Compute };

    // Everything declared with `const` or in comments that configures the pipeline (OptiFine "shader directives").
    struct Directives {
        std::map<std::string, std::string> constants;           // name -> value text (shadowMapResolution, colortex0Format ...)
        // drawBuffers per fragment program, parsed from /* DRAWBUFFERS:012 */ or /* RENDERTARGETS: 0,1,2 */.
        double Number(const std::string& name, double fallback) const;
        bool Bool(const std::string& name, bool fallback) const;
        std::string Text(const std::string& name, const std::string& fallback = {}) const;
    };

    struct ProgramSource {
        std::string name;                 // logical program name, e.g. "gbuffers_terrain"
        std::string sourceName;           // program whose files were actually used (fallbacks)
        ExpandedSource vertex, geometry, fragment;
        std::vector<ExpandedSource> compute;   // compute shaders (".csh"), may have several local sizes
        std::vector<int> drawBuffers;          // colour attachments written (from DRAWBUFFERS / RENDERTARGETS)
        bool hasDrawBuffersTag = false;
        bool found = false;
    };

    // The standard OptiFine/Iris program list with its fallback chain ("" = none).
    struct ProgramInfo {
        const char* name;
        const char* fallback;
    };
    const std::vector<ProgramInfo>& StandardPrograms();
    // Name of the program whose files are used when `name` has none ("" when it has no fallback).
    std::string FallbackFor(const std::string& name);

    class ShaderPackInstance {
    public:
        // `dimension` is the pack folder used for world specific programs ("world0", "world-1", "world1" or "").
        ShaderPackInstance(std::shared_ptr<PackSource> pack, OptionValues overrides, MacroMap engineMacros);

        const PackSource& Source() const { return *pack; }
        const Properties& Props() const { return props; }
        const MacroMap& Macros() const { return macros; }              // engine + option macros (for shaders.properties)
        const MacroMap& EngineMacros() const { return engineMacros; }  // macros injected into shader sources
        const std::vector<ShaderOption>& Options() const { return options; }
        const std::string& Name() const { return packName; }

        void SetDimension(const std::string& folder);
        const std::string& Dimension() const { return dimension; }

        // Finds and expands the files of one program, following the fallback chain.
        std::shared_ptr<ProgramSource> Program(const std::string& name);
        // Names of the existing composite style passes in execution order ("composite", "composite1", ...).
        std::vector<std::string> PassChain(const std::string& base) const;
        bool ProgramExists(const std::string& name) const;
        // The program is switched on in shaders.properties (program.<name>.enabled).
        bool ProgramEnabled(const std::string& name) const;

        const Directives& GetDirectives();
        // Parsed block/entity/item id tables: numeric id -> block names ("minecraft:grass_block:snowy=false" ...).
        const std::map<int, std::vector<std::string>>& BlockIds() const { return blockIds; }
        const std::map<int, std::vector<std::string>>& EntityIds() const { return entityIds; }
        const std::map<int, std::vector<std::string>>& ItemIds() const { return itemIds; }
        // Looks up a lang string (option.NAME, screen.NAME ...) for the given locale, falling back to en_US.
        std::string Lang(const std::string& key, const std::string& locale = "en_US") const;
        std::string FirstError() const { return firstError; }

        // Re-reads shaders.properties etc. after option/dimension changes.
        void Reload(OptionValues overrides);

    private:
        std::string FindFile(const std::string& base, const char* ext) const;
        std::shared_ptr<ProgramSource> Load(const std::string& name);
        void LoadProperties();
        void ScanOptions();
        void LoadIdTables();
        void LoadLang();

        std::shared_ptr<PackSource> pack;
        OptionValues overrides;
        MacroMap engineMacros;
        MacroMap macros;
        Properties props;
        std::vector<ShaderOption> options;
        std::string dimension;
        std::string packName;
        std::map<std::string, std::shared_ptr<ProgramSource>> cache;
        Directives directives;
        bool directivesLoaded = false;
        std::map<int, std::vector<std::string>> blockIds, entityIds, itemIds;
        std::map<std::string, std::map<std::string, std::string>> lang;
        std::string firstError;
    };
}
