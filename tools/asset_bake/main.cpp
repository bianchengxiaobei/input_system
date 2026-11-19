#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <string>

#include "insys/asset/asset_loader.h"

int main(int argc, char** argv) {
    const bool benchmark = argc == 4 && std::string(argv[3]) == "--benchmark";
    if (argc != 3 && !benchmark) {
        std::cerr << "usage: asset_bake <input.json> <output.insysb> [--benchmark]\n";
        return 2;
    }

    try {
        std::ifstream input(argv[1], std::ios::binary);
        if (!input) {
            std::cerr << "asset_bake: cannot open '" << argv[1] << "'\n";
            return 1;
        }
        const std::string json((std::istreambuf_iterator<char>(input)),
                               std::istreambuf_iterator<char>());
        const insys::ActionAsset asset = insys::asset::AssetLoader::load_from_json(json);
        const std::string binary = insys::asset::AssetLoader::to_binary(asset);

        std::ofstream output(argv[2], std::ios::binary);
        if (!output) {
            std::cerr << "asset_bake: cannot create '" << argv[2] << "'\n";
            return 1;
        }
        output.write(binary.data(), static_cast<std::streamsize>(binary.size()));
        if (!output) {
            std::cerr << "asset_bake: failed writing '" << argv[2] << "'\n";
            return 1;
        }

        std::size_t actionCount = 0;
        std::size_t bindingCount = 0;
        for (const insys::ActionMap& map : asset.maps) {
            actionCount += map.actions.size();
            bindingCount += map.bindings.size();
        }
        std::cout << "baked '" << argv[1] << "' -> '" << argv[2] << "': "
                  << asset.maps.size() << " maps, " << actionCount << " actions, "
                  << bindingCount << " bindings, " << asset.controlSchemes.size()
                  << " schemes, " << binary.size() << " bytes\n";

        if (benchmark) {
            constexpr int kIterations = 100;
            volatile std::size_t checksum = 0;

            const auto jsonStart = std::chrono::steady_clock::now();
            for (int i = 0; i < kIterations; ++i) {
                const insys::ActionAsset loaded =
                    insys::asset::AssetLoader::load_from_json(json);
                checksum += loaded.maps.size();
            }
            const auto jsonEnd = std::chrono::steady_clock::now();

            const auto binaryStart = std::chrono::steady_clock::now();
            for (int i = 0; i < kIterations; ++i) {
                const insys::ActionAsset loaded =
                    insys::asset::AssetLoader::load_from_binary(binary);
                checksum += loaded.maps.size();
            }
            const auto binaryEnd = std::chrono::steady_clock::now();

            const auto jsonMs =
                std::chrono::duration<double, std::milli>(jsonEnd - jsonStart).count();
            const auto binaryMs =
                std::chrono::duration<double, std::milli>(binaryEnd - binaryStart).count();
            std::cout << "load benchmark (" << kIterations << "x): json="
                      << std::fixed << std::setprecision(3) << jsonMs
                      << " ms, binary=" << binaryMs << " ms"
                      << " (checksum=" << checksum << ")\n";
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "asset_bake: " << error.what() << '\n';
        return 1;
    }
}
