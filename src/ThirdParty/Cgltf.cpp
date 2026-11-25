// Implémentation de cgltf (lecture) et cgltf_write (écriture glTF), en-têtes seuls fournis par vcpkg (A70). Utilisés par
// src/assets/gltf_import.cpp et gltf_export.cpp.
// cgltf_write mesure la taille du JSON par snprintf(NULL, 0, …) : légal, mais GCC avec _FORTIFY_SOURCE le signale.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic ignored "-Wformat-truncation"
#endif
#define CGLTF_IMPLEMENTATION
#include <cgltf.h>
#undef CGLTF_IMPLEMENTATION // cgltf_write.h inclut cgltf.h : son implémentation ne doit pas être instanciée deux fois
#define CGLTF_WRITE_IMPLEMENTATION
#include <cgltf_write.h>
