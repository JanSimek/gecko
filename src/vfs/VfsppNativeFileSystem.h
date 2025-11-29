#pragma once

// Centralized place to strip Windows API macro overloads before including vfspp.
#ifdef _WIN32
#ifdef CreateFile
#undef CreateFile
#endif
#ifdef CopyFile
#undef CopyFile
#endif
#endif

#include <vfspp/VirtualFileSystem.hpp>
#include <vfspp/NativeFileSystem.hpp>
