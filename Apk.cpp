#include "Apk.h"

#include <ValueVisitor.h>
#include <android-base/stringprintf.h>
#include <dex/dex_file-inl.h>
#include <dex/dex_file.h>
#include <dex/dex_file_loader.h>
#include <io/StringStream.h>
#include <text/Printer.h>

using ::android::ConfigDescription;
using ::android::base::StringPrintf;

namespace apkparser {

class XmlPrinter : public aapt::xml::ConstVisitor {
   public:
    explicit XmlPrinter(aapt::text::Printer* printer, const aapt::LoadedApk* loadedApk)
        : printer_(printer), loadedApk_(loadedApk) {
        // 将id和value对应存储到map
        const ConfigDescription& config = DefaultConfig();
        auto table = loadedApk_->GetResourceTable();
        if (table) {
            for (auto& package : table->packages) {
                for (auto& type : package->types) {
                    for (auto& entry : type->entries) {
                        if (entry->id) {
                            if (auto value = BestConfigValue(entry.get(), config)) {
                                this->idValueMap_[entry->id.value()] = value;
                            }
                        }
                    }
                }
            }
        }
    }

    /*
     * Retrieves a configuration value of the resource entry that best matches the specified
     * configuration.
     */
    static aapt::Value* BestConfigValue(aapt::ResourceEntry* entry, const ConfigDescription& match) {
        if (!entry) {
            return nullptr;
        }

        // Determine the config that best matches the desired config
        aapt::ResourceConfigValue* best_value = nullptr;
        for (auto& value : entry->values) {
            if (!value->config.match(match)) {
                continue;
            }

            if (best_value != nullptr) {
                if (!value->config.isBetterThan(best_value->config, &match)) {
                    if (value->config.compare(best_value->config) != 0) {
                        continue;
                    }
                }
            }

            best_value = value.get();
        }

        // The entry has no values
        if (!best_value) {
            return nullptr;
        }

        return best_value->value.get();
    }

    aapt::Value* FindValueById(const aapt::ResourceId& res_id) {
        if (!this->idValueMap_.empty()) {
            auto it = this->idValueMap_.find(res_id.id);
            if (it != this->idValueMap_.end()) {
                return it->second;
            }
        }
        return nullptr;
    }

    /** Creates a default configuration used to retrieve resources. */
    static ConfigDescription DefaultConfig() {
        ConfigDescription config;
        config.orientation = android::ResTable_config::ORIENTATION_PORT;
        config.density = android::ResTable_config::DENSITY_MEDIUM;
        config.sdkVersion = 10000;  // Very high.
        config.screenWidthDp = 320;
        config.screenHeightDp = 480;
        config.smallestScreenWidthDp = 320;
        config.screenLayout |= android::ResTable_config::SCREENSIZE_NORMAL;
        return config;
    }

    /** Attempts to resolve the reference to a non-reference value. */
    aapt::Value* ResolveReference(aapt::Reference* ref) {
        const int kMaxIterations = 40;
        int i = 0;
        while (ref && ref->id && i++ < kMaxIterations) {
            if (auto value = FindValueById(ref->id.value())) {
                if (aapt::ValueCast<aapt::Reference>(value)) {
                    ref = aapt::ValueCast<aapt::Reference>(value);
                } else {
                    return value;
                }
            }
        }
        return nullptr;
    }

    void Visit(const aapt::xml::Element* el) override {
        // 解析命名空
        printer_->Print(StringPrintf("<%s", el->name.c_str()));
        for (const auto& decl : el->namespace_decls) {
            if (decl.prefix.empty()) {
                printer_->Print(StringPrintf(" xmlns=\"%s\"", decl.uri.c_str()));
            } else {
                printer_->Print(StringPrintf(" xmlns:%s=\"%s\"", decl.prefix.c_str(), decl.uri.c_str()));
            }
        }
        // 解析属性
        for (const auto& attr : el->attributes) {
            // 属性命名空间
            std::string attr_name;
            if (attr.namespace_uri.empty()) {
                attr_name = attr.name;
            } else if (attr.namespace_uri == "http://schemas.android.com/apk/res/android") {
                attr_name = "android:" + attr.name;
            } else {
                attr_name = attr.namespace_uri + ":" + attr.name;
            }
            // 判断属性值是否为引用
            if (attr.compiled_value) {
                aapt::Value* value = attr.compiled_value.get();
                if (aapt::ValueCast<aapt::Reference>(value)) {
                    value = ResolveReference(aapt::ValueCast<aapt::Reference>(value));
                }
                // 打印引用值
                if (value != nullptr) {
                    if (aapt::ValueCast<aapt::String>(value)) {
                        printer_->Print(StringPrintf(" %s=\"%s\"", attr_name.data(), aapt::ValueCast<aapt::String>(value)->value->data()));
                    } else if (aapt::ValueCast<aapt::RawString>(value)) {
                        printer_->Print(StringPrintf(" %s=\"%s\"", attr_name.data(), aapt::ValueCast<aapt::RawString>(value)->value->data()));
                    } else if (aapt::ValueCast<aapt::StyledString>(value)) {
                        printer_->Print(StringPrintf(" %s=\"%s\"", attr_name.data(), aapt::ValueCast<aapt::StyledString>(value)->value.operator*().value.data()));
                    } else if (aapt::ValueCast<aapt::FileReference>(value)) {
                        printer_->Print(StringPrintf(" %s=\"%s\"", attr_name.data(), aapt::ValueCast<aapt::FileReference>(value)->path.operator*().data()));
                    } else if (aapt::ValueCast<aapt::BinaryPrimitive>(value)) {
                        printer_->Print(StringPrintf(" %s=\"%s\"", attr_name.data(), std::to_string(aapt::ValueCast<aapt::BinaryPrimitive>(value)->value.data).data()));
                    } else {
                        printer_->Print(StringPrintf(" %s=\"%s\"", attr_name.data(), attr.value.data()));
                    }
                } else {
                    printer_->Print(StringPrintf(" %s=\"%s\"", attr_name.data(), attr.value.data()));
                }
            } else {
                printer_->Print(StringPrintf(" %s=\"%s\"", attr_name.data(), attr.value.data()));
            }
        }
        if (el->children.empty()) {
            printer_->Println("/>");
        } else {
            printer_->Println(">");
        }
        // 遍历子节点
        printer_->Indent();
        printer_->Indent();
        aapt::xml::ConstVisitor::Visit(el);
        printer_->Undent();
        printer_->Undent();
        // 打印 end tag
        if (!el->children.empty()) {
            printer_->Println(StringPrintf("</%s>", el->name.c_str()));
        }
    }

   private:
    aapt::text::Printer* printer_;
    const aapt::LoadedApk* loadedApk_;
    std::map<aapt::ResourceId, aapt::Value*> idValueMap_;
};

std::unique_ptr<Apk> Apk::LoadApkFromPath(const std::string& path) {
    std::unique_ptr<Apk> apk = std::make_unique<Apk>();
    aapt::StdErrDiagnostics diagnostics;
    auto loadedApk = aapt::LoadedApk::LoadApkFromPath(path, &diagnostics);
    if (!loadedApk) {
        return {};
    }
    apk->loadedApk = std::move(loadedApk);
    return apk;
}

std::unique_ptr<std::string> Apk::GetManifest() const {
    const aapt::xml::XmlResource* manifest = this->loadedApk->GetManifest();
    if (manifest == nullptr) {
        std::cerr << "failed to find " << aapt::kAndroidManifestPath << std::endl;
        return {};
    }
    std::unique_ptr<std::string> result(new std::string());
    {  // printer 析构刷新缓冲区
        aapt::io::StringOutputStream sout(result.get());
        aapt::text::Printer printer(&sout);
        XmlPrinter xml_visitor(&printer, this->loadedApk.get());
        manifest->root->Accept(&xml_visitor);
    }
    return result;
}

std::unique_ptr<std::list<std::string>> Apk::GetStrings() const {
    // 获取资源文件的字符串池
    const aapt::ResourceTable* table = this->loadedApk->GetResourceTable();
    if (table == nullptr) {
        std::cerr << "failed to find resource table" << std::endl;
        return {};
    }
    // 遍历字符串池
    std::unique_ptr<std::list<std::string>> result(new std::list<std::string>());
    for (const auto& entry : table->string_pool.strings()) {
        if (entry.get()->value.empty()) {
            continue;
        }
        std::string str = Apk::TrimString(entry.get()->value);
        if (str.empty()) continue;
        result.get()->push_back(str);
    }
    return result;
}

std::unique_ptr<std::pair<std::set<std::string>, std::set<std::string>>> Apk::ParseDexes() const {
    // 提取apk中的所有dex
    std::vector<aapt::io::IFile*> dexes;
    auto collection = this->loadedApk->GetFileCollection();
    auto iter = collection->Iterator();
    while (iter.get()->HasNext()) {
        auto file = iter.get()->Next();
        if (file->GetSource().path.rfind(".dex") != std::string::npos) {
            dexes.push_back(file);
        }
    }
    // 解析dex
    std::unique_ptr<std::pair<std::set<std::string>, std::set<std::string>>> result(new std::pair<std::set<std::string>, std::set<std::string>>);
    for (auto&& file : dexes) {
        file->OpenAsData();
        std::unique_ptr<aapt::io::IData> data = file->OpenAsData();
        if (data == nullptr || data->size() < 4) {
            continue;
        }
        const uint8_t* base = reinterpret_cast<const uint8_t*>(data->data());
        size_t size = data->size();
        const std::string location = file->GetSource().path;
        art::DexFileLoader dexFileLoader;
        uint32_t magic = *reinterpret_cast<const uint32_t*>(base);
        if (!dexFileLoader.IsMagicValid(magic)) {
            continue;
        }
        std::string error_msg;
        const art::DexFile::Header* dex_header = reinterpret_cast<const art::DexFile::Header*>(base);
        std::unique_ptr<const art::DexFile> dexFile = dexFileLoader.Open(base,
                                                                         size,
                                                                         location,
                                                                         dex_header->checksum_,
                                                                         /*oat_dex_file=*/nullptr,
                                                                         false,
                                                                         false,
                                                                         &error_msg);
        if (dexFile == nullptr) {
            continue;
        }
        // 遍历类
        for (uint32_t i = 0; i < dexFile->NumClassDefs(); ++i) {
            const char* descriptor = dexFile->GetClassDescriptor(dexFile->GetClassDef(i));
            if (descriptor == nullptr || strlen(descriptor) == 0) {
                continue;
            }
            // 去除首尾的 L 和; 将 / 转换为 .
            std::string className(descriptor + 1, strlen(descriptor) - 2);
            std::replace(className.begin(), className.end(), '/', '.');
            // 截断匿名类, 存在多个匿名类的情况
            auto pos = className.find("$");
            if (pos != std::string::npos) {
                className = className.substr(0, pos);
            }
            result.get()->first.insert(className);
        }

        // 遍历字符串
        for (uint32_t i = 0; i < dexFile->NumStringIds(); ++i) {
            const char* str = dexFile->GetStringData(dexFile->GetStringId(art::dex::StringIndex(i)));
            if (str == nullptr || strlen(str) == 0) {
                continue;
            }
            std::string str2 = Apk::TrimString(str);
            if (str2.empty()) continue;
            result.get()->second.insert(str2);
        }
    }

    return result;
}

std::unique_ptr<nlohmann::json> Apk::DoAllTasks() const {
    // 解析manifest
    // auto now = std::chrono::system_clock::now();
    std::unique_ptr<std::string> manifest = this->GetManifest();
    if (!manifest || manifest.get()->empty()) {
        std::cerr << "parse manifest failed" << std::endl;
        return {};
    }
    // auto end = std::chrono::system_clock::now();
    // auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - now);
    // std::cout << "parse manifest cost " << duration.count() << "ms" << std::endl;
    // 解析资源字符串
    // now = std::chrono::system_clock::now();
    auto strings = this->GetStrings();
    if (!strings || strings.get()->empty()) {
        std::cerr << "parse strings failed" << std::endl;
        return {};
    }
    // end = std::chrono::system_clock::now();
    // duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - now);
    // std::cout << "parse strings cost " << duration.count() << "ms" << std::endl;
    // 解析dexes
    // now = std::chrono::system_clock::now();
    auto dexes = this->ParseDexes();
    if (!dexes || dexes.get()->first.empty() || dexes.get()->second.empty()) {
        std::cerr << "parse dexes failed" << std::endl;
        return {};
    }
    // end = std::chrono::system_clock::now();
    // duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - now);
    // std::cout << "parse dexes cost " << duration.count() << "ms" << std::endl;
    // 构造json
    std::unique_ptr<nlohmann::json> result(new nlohmann::json());
    result.get()->operator[]("manifest") = *manifest.get();
    nlohmann::json resStrings;
    resStrings["strings"] = *strings.get();
    result.get()->operator[]("resources.arsc") = resStrings;
    result.get()->operator[]("dex_classes") = dexes.get()->first;
    result.get()->operator[]("dex_strings") = dexes.get()->second;
    return result;
}

}  // namespace apkparser