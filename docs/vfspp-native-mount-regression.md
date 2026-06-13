# `NativeFileSystem` with an absolute base path mounted at `/` resolves wrong virtual paths (regression after v2.1.1)

## Summary

After `v2.1.1`, `FileInfo::Configure()` strips `aliasPath` **before** `basePath`. Because
`aliasPath` is checked with `fileName.find(aliasPath) == 0`, a root alias of `"/"` matches **every**
absolute path (they all start with `/`). As a result, a `NativeFileSystem` created with an **absolute**
base path and mounted at alias `"/"` no longer strips the base path, so every file's `VirtualPath()`
becomes its full absolute path instead of the intended alias-relative path. Files can no longer be
opened by their virtual path.

DAT/ZIP-style filesystems are unaffected because their entry names are *relative* and never begin with
`/`.

## Affected versions

- **Last good:** `v2.1.1`
- **First broken:** `487b922298881e53de0188ed3ae815590bdb2083` — *"Fix issue with duplicating alias path when creating a new file"* (Yev, 2026-05-06), i.e. current `master`.

The regressing change to `include/vfspp/FileInfo.hpp`:

```diff
 void Configure(const std::string& aliasPath, const std::string& basePath, const std::string& fileName)
 {
-    // Remove alias path from file name if any
     std::string strippedFileName = fileName;
-    if (!basePath.empty() && fileName.find(basePath) == 0) {
+    if (!aliasPath.empty() && fileName.find(aliasPath) == 0) {
+        strippedFileName = fileName.substr(aliasPath.length());
+    } else if (!basePath.empty() && fileName.find(basePath) == 0) {
         strippedFileName = fileName.substr(basePath.length());
     }
```

## Minimal reproduction (just `FileInfo`)

```cpp
#include <vfspp/FileInfo.hpp>
#include <cassert>

int main()
{
    // alias "/", an ABSOLUTE base path, and a file discovered under it
    // (this is exactly what NativeFileSystem builds for each entry).
    vfspp::FileInfo info("/", "/home/user/game", "/home/user/game/data/maps/arvillag.map");

    // Expected: base path stripped, alias prepended.
    //   v2.1.1  -> "/data/maps/arvillag.map"                      (correct)
    //   master  -> "/home/user/game/data/maps/arvillag.map"      (base path NOT stripped)
    assert(info.VirtualPath() == "/data/maps/arvillag.map");
}
```

## Full reproduction (`NativeFileSystem` + `VirtualFileSystem`)

```cpp
#include <vfspp/VirtualFileSystem.hpp>
#include <vfspp/NativeFileSystem.hpp>
#include <cassert>
#include <memory>

using namespace vfspp;

int main()
{
    // On disk: /home/user/game/data/maps/arvillag.map  (absolute base directory)
    auto vfs = std::make_shared<VirtualFileSystem>();
    auto nfs = std::make_shared<NativeFileSystem>("/", "/home/user/game"); // absolute base path
    nfs->Initialize();
    vfs->AddFileSystem("/", nfs);

    auto file = vfs->OpenFile("/data/maps/arvillag.map", IFile::FileMode::Read);

    assert(file != nullptr); // passes on v2.1.1, FAILS on master
}
```

## Expected vs. actual

| | `VirtualPath()` for a file at `<base>/data/maps/arvillag.map` |
|---|---|
| **Expected** (v2.1.1) | `/data/maps/arvillag.map` |
| **Actual** (master) | `/home/user/game/data/maps/arvillag.map` |

`vfs->OpenFile("/data/maps/arvillag.map", …)` then returns `nullptr`, and the file only resolves if you
ask for its full absolute path — which defeats the purpose of the alias.

## Root cause

`NativeFileSystem::BuildFilelist()` constructs each entry as
`FileInfo(aliasPath, basePath, entry.path().string())`, where `entry.path()` is **absolute**. In
`Configure()`, the new alias-first branch:

```cpp
if (!aliasPath.empty() && fileName.find(aliasPath) == 0) {        // aliasPath == "/"
    strippedFileName = fileName.substr(aliasPath.length());      // strips only the leading "/"
}
```

short-circuits before the base-path branch whenever `aliasPath` is a prefix of `fileName`. With the
root alias `"/"`, that is true for **all** absolute paths, so the base path is never removed and
`m_VirtualPath = aliasPath / fileName-without-leading-slash` ends up being the whole absolute path.

The alias-first branch is useful for `CreateFile(virtualPath)`, where `fileName` is already a virtual
path that legitimately begins with the alias — but it is too greedy for the trivial `"/"` alias.

## Suggested fix

Prefer the base-path strip (the native prefix) and fall back to the alias strip — both original cases
keep working, since a `CreateFile` virtual path does not begin with the absolute `basePath`:

```cpp
if (!basePath.empty() && fileName.find(basePath) == 0) {
    strippedFileName = fileName.substr(basePath.length());
} else if (!aliasPath.empty() && fileName.find(aliasPath) == 0) {
    strippedFileName = fileName.substr(aliasPath.length());
}
```

Alternatively, when both prefixes match, strip the **longer** (more specific) one, or skip the alias
strip when `aliasPath == "/"`. A regression test mounting an absolute directory at `"/"` and asserting
`OpenFile("/<relative>")` succeeds would guard this.

## Workaround

Pin to the `v2.1.1` release tag (which strips the base path only).

## Environment

- vfspp `master` @ `487b922`
- Apple clang 21, macOS, libc++, C++20
- Reproducible on any platform with an absolute base directory mounted at alias `"/"`.
