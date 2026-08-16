#include "SBConfig.h"

#include <iostream>

int main()
{
    const auto document = SB::Cfg::Parse(
        "[Native]\n"
        "EngineFixes = false ; public archives ship engine writes disabled\n"
        "Literal = Data/Textures;Alternate\n");

    const auto* native = document.Find("Native");
    if (native == nullptr) {
        std::cerr << "Native section was not parsed\n";
        return 1;
    }

    if (native->Bool("EngineFixes", true)) {
        std::cerr << "inline comment caused false to fall back to true\n";
        return 1;
    }

    if (native->Get("Literal") != "Data/Textures;Alternate") {
        std::cerr << "embedded semicolon was mistaken for an inline comment\n";
        return 1;
    }

    return 0;
}
