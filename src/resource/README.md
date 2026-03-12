# Resource Layer

## Overview

The editor uses an injected `geck::resource::GameResources` object instead of a global singleton.
`GameResources` owns four focused services:

- `DataFileSystem`: mounts native directories and DAT archives, lists files, reads raw bytes, and reports file origins
- `ResourceRepository`: parses and caches `IFile` resources through `ReaderFactory::readFileFromMemory`
- `FrmResolver`: converts engine FIDs into FRM paths using the same LST-driven rules as Fallout 2 CE
- `TextureManager`: creates and caches SFML textures, including FRM + palette conversion

## Usage

```cpp
auto resources = std::make_shared<geck::resource::GameResources>();
resources->files().addDataPath("/path/to/fallout2");

const auto* tiles = resources->repository().load<geck::Lst>("art/tiles/tiles.lst");
const auto& texture = resources->textures().get("art/items/knife.frm");
const std::string frmPath = resources->frmResolver().resolve(fid);
```

UI and editor classes should receive `GameResources` through constructors:

```cpp
class EditorWidget : public QWidget {
public:
    EditorWidget(geck::resource::GameResources& resources,
        std::unique_ptr<geck::Map> map,
        QWidget* parent = nullptr);
};
```

## Design Rules

- Do not add global accessors or compatibility shims.
- Do not expose raw VFS handles to UI code.
- Load parsed resources through `ResourceRepository`, not directly from the filesystem.
- Load display values from game data files and match Fallout 2 CE behavior.
- Keep `TextureManager` as the only place that creates SFML textures from FRM data.

## Thread Ownership

- `DataFileSystem::addDataPath()` and `GameResources::clearAllDataPaths()` are lifecycle operations.
- `ResourceRepository::load<T>()` may be used on loader threads.
- `TextureManager` is main-thread only because it creates SFML/OpenGL textures.

## Typical Migrations

- `repository().load<T>(path)` replaces parsed-resource singleton lookups
- `files().readRawBytes(path)` replaces ad-hoc VFS file reads
- `frmResolver().resolve(fid)` replaces direct FID-to-path helpers
- `textures().get(path)` replaces texture singleton access
