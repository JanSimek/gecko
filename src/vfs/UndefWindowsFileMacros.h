// No include guard on purpose: include this after any header that may pull in <windows.h>.
//
// vfspp's IFileSystem declares CreateFile / CopyFile. On Windows <windows.h> rewrites those names to
// ...A, which breaks the declarations and their overrides, so strip the macros before using vfspp.
#ifdef _WIN32
#ifdef CreateFile
#undef CreateFile
#endif
#ifdef CopyFile
#undef CopyFile
#endif
#endif
