#include "Apk.h"

#include <ValueVisitor.h>
#include <android-base/stringprintf.h>
#include <androidfw/AssetManager.h>
#include <dex/dex_file-inl.h>
#include <dex/dex_file.h>
#include <dex/dex_file_loader.h>
#include <io/StringStream.h>
#include <io/ZipArchive.h>
#include <text/Printer.h>
#include <utils/String8.h>

using ::android::ConfigDescription;
using ::android::base::StringPrintf;

namespace apkparser {

class XmlPrinter : public aapt::xml::ConstVisitor {
   private:
    aapt::text::Printer* printer_;
    const Apk* apk_;

   public:
    explicit XmlPrinter(aapt::text::Printer* printer, const Apk* apk) : printer_(printer), apk_(apk){};

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
            std::string attr_value = attr.value;
            // 判断属性值是否为引用
            if (attr.compiled_value) {
                aapt::Value* value = attr.compiled_value.get();
                if (aapt::ValueCast<aapt::Reference>(value)) {
                    // 将aapt::Reference转换为android::Res_value
                    auto refValue = aapt::ValueCast<aapt::Reference>(value);
                    android::Res_value resValue;
                    resValue.dataType = android::Res_value::TYPE_REFERENCE;
                    resValue.data = refValue->id.value().id;
                    const android::ResTable& resTable = apk_->GetAssetManager()->getResources(false);
                    // 判断packages中是否存在该资源
                    auto resInPackages = [](const android::ResTable& resTable, const android::Res_value& resValue) {
                        for (size_t i = 0; i < resTable.getBasePackageCount(); i++) {
                            if (resTable.getBasePackageId(i) == (resValue.data >> 24)) {
                                return true;
                            }
                        }
                        return false;
                    };
                    // 解决引用
                    if (resInPackages(resTable, resValue)) {
                        ssize_t block = resTable.resolveReference(&resValue, 0);
                        if (block >= 0) {
                            if (resValue.dataType == android::Res_value::TYPE_STRING) {
                                size_t len;
                                const char16_t* str = resTable.valueToString(&resValue, static_cast<size_t>(block), NULL, &len);
                                attr_value = str ? android::String8(str, len) : "";
                            }
                        }
                    }
                } else {
                    aapt::io::StringOutputStream sout(&attr_value);
                    aapt::text::Printer p(&sout);
                    if (aapt::ValueCast<aapt::String>(value)) {
                        attr_value = *(aapt::ValueCast<aapt::String>(value)->value)->data();
                    } else if (aapt::ValueCast<aapt::RawString>(value)) {
                        attr_value = *(aapt::ValueCast<aapt::RawString>(value)->value)->data();
                    } else if (aapt::ValueCast<aapt::BinaryPrimitive>(value)) {
                        aapt::ValueCast<aapt::BinaryPrimitive>(value)->PrettyPrint(&p);
                    }
                }
            }
            // 参考mobsf,替换字符串中的`“`为`&quot;`
            std::string::size_type pos = 0;
            while ((pos = attr_value.find("\"", pos)) != std::string::npos) {
                attr_value.replace(pos, 1, "&quot;");
                pos += 6;
            }
            printer_->Print(StringPrintf(" %s=\"%s\"", attr_name.data(), android::ResTable::normalizeForOutput(attr_value.data()).c_str()));
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
};

std::unique_ptr<Apk> Apk::LoadApkFromPath(const std::string& path) {
    // 加载zip文件
    aapt::Source source(path);
    std::string error;
    std::unique_ptr<aapt::io::ZipFileCollection> collection = aapt::io::ZipFileCollection::Create(path, &error);
    if (!collection) {
        std::cerr << "failed opening zip: " << error << std::endl;
        return {};
    }
    // 读取resources.arsc文件, 解析资源.
    std::unique_ptr<android::AssetManager> assetManager(new android::AssetManager());
    assetManager.get()->addAssetPath(android::String8(path.c_str()), NULL);
    android::ResTable_config config;
    memset(&config, 0, sizeof(android::ResTable_config));
    config.language[0] = 'e';
    config.language[1] = 'n';
    config.country[0] = 'U';
    config.country[1] = 'S';
    config.orientation = android::ResTable_config::ORIENTATION_PORT;
    config.density = android::ResTable_config::DENSITY_MEDIUM;
    config.sdkVersion = 10000;  // Very high.
    config.screenWidthDp = 320;
    config.screenHeightDp = 480;
    config.smallestScreenWidthDp = 320;
    config.screenLayout |= android::ResTable_config::SCREENSIZE_NORMAL;
    assetManager.get()->setConfiguration(config);
    const android::ResTable& res = assetManager.get()->getResources(false);
    if (res.getError() != android::NO_ERROR) {
        std::cerr << "failed loading resource" << std::endl;
        return {};
    }
    // 解析AndroidManifest.xml
    aapt::io::IFile* manifest_file = collection->FindFile(kAndroidManifestPath);
    if (manifest_file == nullptr) {
        std::cerr << "failed loading " << kAndroidManifestPath << std::endl;
        return {};
    }
    std::unique_ptr<aapt::io::IData> manifest_data = manifest_file->OpenAsData();
    if (manifest_data == nullptr) {
        std::cerr << "failed loading " << kAndroidManifestPath << std::endl;
        return {};
    }
    std::unique_ptr<aapt::xml::XmlResource> manifest = aapt::xml::Inflate(manifest_data->data(), manifest_data->size(), &error);
    if (manifest == nullptr) {
        std::cerr << "failed to parse binary " << kAndroidManifestPath << ": " << error << std::endl;
        return {};
    }
    std::unique_ptr<Apk> result(new Apk(std::move(collection), std::move(assetManager), std::move(manifest)));
    return result;
}

std::unique_ptr<std::string> Apk::GetManifest() const {
    std::unique_ptr<std::string> result(new std::string());
    {  // printer 析构刷新缓冲区
        aapt::io::StringOutputStream sout(result.get());
        aapt::text::Printer printer(&sout);
        XmlPrinter xml_visitor(&printer, this);
        this->manifest_->root->Accept(&xml_visitor);
    }
    return result;
}

std::unique_ptr<std::list<std::string>> Apk::GetStrings() const {
    // 读取字符串池
    const android::ResStringPool* pool = assetManager_.get()->getResources(false).getTableStringBlock(0);
    // 打印字符串
    if (pool->getError() == android::NO_INIT) {
        std::cerr << "string pool is unitialized" << std::endl;
        return {};
    } else if (pool->getError() != android::NO_ERROR) {
        std::cerr << "string pool is corrupt/invalid." << std::endl;
        return {};
    }
    std::unique_ptr<std::list<std::string>> result(new std::list<std::string>());
    for (size_t i = 0; i < pool->size(); i++) {
        auto str = pool->string8ObjectAt(i);
        if (str.has_value() && strlen(str.value().string()) > 0) {
            result.get()->push_back(Apk::TrimString(str.value().string()));
        }
    }
    return result;
}

std::unique_ptr<std::pair<std::set<std::string>, std::set<std::string>>> Apk::ParseDexes() const {
    // 提取apk中的所有dex
    std::vector<aapt::io::IFile*> dexes;
    auto collection = this->collection_.get();
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
    // auto now = std::chrono::system_clock::now();
    auto dexes = this->ParseDexes();
    if (!dexes || dexes.get()->first.empty() || dexes.get()->second.empty()) {
        std::cerr << "parse dexes failed" << std::endl;
        return {};
    }
    // auto end = std::chrono::system_clock::now();
    // auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - now);
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