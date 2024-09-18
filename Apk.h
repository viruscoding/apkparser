#ifndef APKPARSER_APK_H
#define APKPARSER_APK_H

#include <androidfw/AssetManager.h>
#include <io/File.h>
#include <xml/XmlDom.h>

#include <json.hpp>
#include <set>

namespace apkparser {

constexpr static const char kApkResourceTablePath[] = "resources.arsc";
constexpr static const char kAndroidManifestPath[] = "AndroidManifest.xml";

class Apk {
   private:
    std::unique_ptr<aapt::io::IFileCollection> collection_;
    std::unique_ptr<android::AssetManager> assetManager_;

   public:
    Apk(std::unique_ptr<aapt::io::IFileCollection> collection, std::unique_ptr<android::AssetManager> assetManager)
        : collection_(std::move(collection)), assetManager_(std::move(assetManager)){};
    ~Apk() = default;

    android::AssetManager* GetAssetManager() const {
        return assetManager_.get();
    }

    aapt::io::IFileCollection* GetFileCollection() const {
        return collection_.get();
    }

    /// @brief zip格式加载apk、即使resources.arsc、AndroidManifest.xml不存在，也可以加载
    /// @param path apk路径
    /// @return 失败返回nullptr
    static std::unique_ptr<Apk> LoadApkFromPath(const std::string& path);

    /// @brief 解析manifest
    /// @return 失败返回nullptr, 没有resources.arsc和AndroidManifest.xml返回空字符串
    std::unique_ptr<std::string> GetManifest() const;

    /// @brief 获取resource.arsc中的字符串池
    /// @return 失败返回nullptr, 没有resources.arsc或其中没有字符串,返回空字符串列表
    std::unique_ptr<std::list<std::string>> GetStrings() const;

    /// @brief 解析所有dex的class和string
    /// @return 永远不会返回nullptr, 没有dex返回空列表
    std::unique_ptr<std::pair<std::set<std::string>, std::set<std::string>>> ParseDexes() const;

    /// @brief 执行所有的任务, 并返回json
    /// @return 某个任务失败返回nullptr
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