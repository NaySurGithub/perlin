#pragma once
#include <string>

namespace perlin::blockuse {

    enum class Use { Nothing, Interact, Place };

    bool IsReplaceable(const std::string& blockName);

    bool IsInteractive(const std::string& blockName);

    struct Result {
        Use use = Use::Nothing;
        int dest[3] = {0, 0, 0};
    };

    Result Classify(const std::string& clickedName, const std::string& neighbourName, int clicked[3], int face, bool holdsItem,
                    bool holdsBlock, bool sneaking);
}
