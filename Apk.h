#ifndef APKPARSER_APK_H
#define APKPARSER_APK_H

#include <LoadedApk.h>
#include <json.hpp>

namespace apkparser {

class Apk {
   private:
    std::unique_ptr<aapt::LoadedApk> loadedApk;

   public:
    Apk() = default;
    ~Apk() = default;

    static std::unique_ptr<Apk> LoadApkFromPath(const std::string& path);

    std::unique_ptr<std::string> GetManifest() const;

    std::unique_ptr<std::list<std::string>> GetStrings() const;

    std::unique_ptr<std::pair<std::set<std::string>, std::set<std::string>>> ParseDexes() const;

    std::unique_ptr<nlohmann::json> DoAllTasks() const;

    // 删除字符串中的\r \n \t 空格
    static std::string TrimString(std::string str) {
        str.erase(std::remove(str.begin(), str.end(), '\r'), str.end());
        str.erase(std::remove(str.begin(), str.end(), '\n'), str.end());
        str.erase(std::remove(str.begin(), str.end(), '\t'), str.end());
        str.erase(std::remove(str.begin(), str.end(), ' '), str.end());
        return str;
    }
};

}  // namespace apkparser

#endif  // APKPARSER_APK_H