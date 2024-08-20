# apkparser

基于aapt2和libdexfile两个模块，封装一个命令行工具，用于解析apk中的资源和dex。

## Todo list

- 加速编译

## 功能

- 解析manifest
- 提取asrc中所有字符串
- 解析dex所有类名和字符串

## 使用方法

```bash
# 解析manifest
apkparser manifest <filename>
# 输出到stdout:
# {
#     "displayNames": [
#         ""
#     ],
#     "manifest": ""
# }

# 提取asrc中所有字符串
apkparser strings <filename>
# 输出到stdout: 按行输出字符串

# 解析dex所有类名和字符串
apkparser dexes <filename>
# 输出到stdout:
# {
#     "dex_strings": [
#         ""
#     ],
#     "dex_classes": [
#         ""
#     ]
# }

# 以上命令合并
apkparser all <filename>
# 输出到stdout:
# {
#     "resources.arsc": {
#         "strings": [
#             ""
#         ]
#     },
#     "dex_strings": [
#         ""
#     ],
#     "displayNames": [
#         ""
#     ],
#     "dex_classes": [
#         ""
#     ],
#     "manifest": ""
# }
```

## 编译安装

先下载Android源码，然后将本项目放到`frameworks/base/tools/`目录下，通过`mm apkparser`命令编译。

### 下载Android源码

```bash
curl https://storage.googleapis.com/git-repo-downloads/repo > /usr/local/repo
chmod +x /usr/local/repo

mkdir platform-tools && cd platform-tools
repo init -u https://android.googlesource.com/platform/manifest.git -b platform-tools-33.0.3 --depth 1
repo sync
```

### 准备环境

下载apkparser源码

```bash
cd frameworks/base/tools/ && git clone https://github.com/fork-ai/apkparser
```

导出libdexfile模块的headers

```c
// 在Android源码根目录，进入art/libdexfile，打开Android.bp，增加以下内容
cc_library_headers {
    name: "libdexfile_headers",
    visibility: ["//visibility:public"],
    host_supported: true,
    export_include_dirs: ["."],
}

// 同时在cc_library模块中添加以下内容
cc_library {
    visibility: ["//visibility:public"], // 增加这一行
    ......
}
```

导出aapt2模块的headers

```c
// 在Android源码根目录，进入frameworks/base/tools/aapt2，打开Android.bp，增加以下内容
cc_library_headers {
    name: "libaapt2_headers",
    visibility: ["//visibility:public"],
    host_supported: true,
    export_include_dirs: ["."],
}
```

### 编译

```bash
# 在Android源码根目录执行下行命令，初始化构建环境
. build/envsetup.sh

# 编译apkparser
mm apkparser

# 调用strips命令减小体积
strips out/host/linux-x86/bin/apkparser
```
