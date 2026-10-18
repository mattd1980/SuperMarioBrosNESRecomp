# Injected into SuperMarioBrosRecomp's project() on Android builds
# (-DCMAKE_PROJECT_SuperMarioBrosRecomp_INCLUDE=this file): SDL2 from source as
# libSDL2.so, so nesrecomp's cyc.cmake finds the SDL2::SDL2 target instead of
# the bundled Windows SDL. The game itself then builds as libmain.so.
if(ANDROID)
    if(NOT SMB_ANDROID_SDL_ROOT OR NOT EXISTS "${SMB_ANDROID_SDL_ROOT}/CMakeLists.txt")
        message(FATAL_ERROR "SMB_ANDROID_SDL_ROOT must name an SDL2 source tree")
    endif()
    # SDL's Android SDL_GetBasePath returns NULL; the host looks beside the
    # executable for config, mods and assets, which the app unpacks into its
    # internal storage. Make the base path that storage (idempotent).
    set(_fs "${SMB_ANDROID_SDL_ROOT}/src/filesystem/android/SDL_sysfilesystem.c")
    file(READ "${_fs}" _fs_text)
    if(NOT _fs_text MATCHES "SDL_GetBasePath\\(void\\)[\r\n]+{[\r\n ]+return SDL_GetPrefPath\\(NULL, NULL\\);")
        string(REGEX REPLACE "char \\*SDL_GetBasePath\\(void\\)[\r\n]+{[^}]*}"
            "char *SDL_GetBasePath(void)\n{\n    return SDL_GetPrefPath(NULL, NULL); /* SMB recomp: internal storage */\n}"
            _fs_patched "${_fs_text}")
        if(_fs_patched STREQUAL _fs_text)
            message(FATAL_ERROR "Could not find SDL_GetBasePath in ${_fs}")
        endif()
        file(WRITE "${_fs}" "${_fs_patched}")
        message(STATUS "Patched SDL_GetBasePath for Android: ${_fs}")
    endif()
    set(SDL_SHARED ON CACHE BOOL "" FORCE)
    set(SDL_STATIC OFF CACHE BOOL "" FORCE)
    set(SDL_TEST OFF CACHE BOOL "" FORCE)
    set(SDL2_DISABLE_SDL2MAIN ON CACHE BOOL "" FORCE)
    add_subdirectory("${SMB_ANDROID_SDL_ROOT}" "${CMAKE_BINARY_DIR}/sdl_build")
    if(NOT TARGET SDL2::SDL2)
        add_library(SDL2::SDL2 ALIAS SDL2)
    endif()
endif()
