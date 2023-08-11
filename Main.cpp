#include <Apk.h>
#include <android-base/logging.h>

#include <json.hpp>

using ::android::StringPiece;

void printUseage() {
    std::cout << "Usage: apkparser <command> <apk_path>" << std::endl;
    std::cout << "Commands:" << std::endl;
    std::cout << "\tmanifest: print the manifest of the apk" << std::endl;
    std::cout << "\tstrings: print the resourdes strings of the apk" << std::endl;
    std::cout << "\tdexes: json print the dexes of the apk" << std::endl;
    std::cout << "\tall: json print the manifest, strings and dexes of the apk " << std::endl;
}

/**
 * 1. 解析manifest
 * 2. 解析资源字符串
 * 3. 解析dex类名和字符串
 */
int main(int argc, char** argv) {
    // Collect the arguments starting after the program name and command name.
    std::vector<StringPiece> args;
    for (int i = 1; i < argc; i++) {
        args.push_back(argv[i]);
    }
    if (args.size() != 2) {
        printUseage();
        return -1;
    }
    std::string command = args[0].to_string();
    std::string path = args[1].to_string();
    // 加载apk
    auto apk = apkparser::Apk::LoadApkFromPath(path);
    if (!apk) {
        std::cerr << "load apk failed" << std::endl;
        return -1;
    }
    if (command == "manifest") {
        // 解析manifest
        std::unique_ptr<std::string> manifest = apk->GetManifest();
        if (!manifest || manifest.get()->empty()) {
            std::cerr << "parse manifest failed" << std::endl;
            return -1;
        }
        std::cout << *manifest.get() << std::endl;
    } else if (command == "strings") {
        // 解析资源字符串
        auto strings = apk->GetStrings();
        if (!strings || strings.get()->empty()) {
            std::cerr << "parse strings failed" << std::endl;
            return -1;
        }
        for (const auto& str : *strings.get()) {
            std::cout << str << std::endl;
        }
    } else if (command == "dexes") {
        // 解析dexes
        auto dexes = apk->ParseDexes();
        if (!dexes || dexes.get()->first.empty() || dexes.get()->second.empty()) {
            std::cerr << "parse dexes failed" << std::endl;
            return -1;
        }
        // 按照json格式输出
        nlohmann::json json;
        json["dex_classes"] = dexes.get()->first;
        json["dex_strings"] = dexes.get()->second;
        std::cout << json.dump(4, ' ', false, nlohmann::detail::error_handler_t::ignore) << std::endl;
    } else if (command == "all") {
        auto json = apk->DoAllTasks();
        if (!json || json.get()->empty()) {
            std::cerr << "parse all failed" << std::endl;
            return -1;
        }
        std::cout << json.get()->dump(4, ' ', false, nlohmann::detail::error_handler_t::ignore) << std::endl;
    } else {
        printUseage();
        return -1;
    }
    return 0;
}