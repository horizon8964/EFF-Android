#include "../third_party/eff/src/eiem_static_resource_sources.h"
#include <iostream>

int wmain(int argc, wchar_t** argv) {
    try {
        using namespace EiemDirectory;
        Require(argc == 5, "compile_si_android <Mod> <selection.bin> <game-root> <output>");
        const auto mod = std::filesystem::canonical(argv[1]);
        const auto selected = EiemStaticSources::Decode(Read(argv[2]));
        const auto game = std::filesystem::canonical(argv[3]);
        const auto output = std::filesystem::absolute(argv[4]);
        auto package = Load(mod);
        std::cout << "Author loaded: " << package.id << std::endl;
        EiemStaticSources::Ensure(selected, output / "baseline", output / "discovery", package, game);
        std::cout << "Android Mesh / Prefab consumer discovery verified" << std::endl;
        auto stage = [](const char* name, size_t n, size_t total) {
            std::cout << "BUILD " << name << ' ' << n << '/' << total << std::endl;
        };
        auto plan = EiemResourceReloadPlan::Build(package, output / "baseline", output / "generation",
            "si-android-startup-1", {}, stage, {}, {}, false, game);
        std::vector<std::string> roots;
        const auto originals = EiemResourceReloadPlan::ReadBaseline(output / "baseline", roots, game);
        std::ofstream routes(output / "routes.tsv", std::ios::binary);
        routes << "EFF_ANDROID_ROUTES\t1\n";
        size_t changed = 0;
        for (const auto& original : originals) {
            const auto patched = output / "generation" / "startup" / original.file;
            if (!std::filesystem::is_regular_file(patched)) continue;
            const auto bytes = Read(patched);
            if (EiemResourceReloadPlan::Hash(bytes) == original.hash) continue;
            std::string logical;
            for (const auto& e : selected.inputs) if (e.second.physical == original.physical && e.second.offset == original.offset && e.second.size == original.size) logical = e.first;
            Require(!logical.empty(), "changed source lost logical identity");
            routes << logical << '\t' << original.physical.generic_u8string() << '\t' << original.offset << '\t'
                << original.size << '\t' << original.hash << '\t' << original.file << '\t' << bytes.size() << '\t'
                << EiemResourceReloadPlan::Hash(bytes) << '\n';
            ++changed;
        }
        routes.close();
        std::cout << "ANDROID READY changed=" << changed << " parts=" << plan.parts.size() << " roots=" << plan.roots.size() << std::endl;
        Require(changed != 0, "compiler produced no Android replacements");
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "ANDROID COMPILE REJECTED: " << e.what() << std::endl;
        return 1;
    }
}
