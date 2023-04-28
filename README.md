## 编译方法

- libdexfile 设置visibility: public, 并导出headers

```c
// 新增
cc_library_headers {
    name: "libdexfile_headers",
    visibility: ["//visibility:public"],
    host_supported: true,
    export_include_dirs: ["."],
}
```

- aapt2 导出headers

```c
// 新增
cc_library_headers {
    name: "libaapt2_headers",
    visibility: ["//visibility:public"],
    host_supported: true,
    export_include_dirs: ["."],
}
```

- 编译 `mm apkparser`
- 删除符号,减小体积 `strips apkparser`
