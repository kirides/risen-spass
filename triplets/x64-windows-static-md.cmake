# Overlay of vcpkg's built-in x64-windows-static-md (static libraries, dynamic CRT), with one addition.
#
# Since MSVC 14.40 (VS 2022 17.10) std::mutex has a constexpr constructor that no longer initialises the lock through
# the runtime; an older MSVCP140.dll on the player's machine (an old VC++ redistributable, or the one in a Proton/Wine
# prefix) then dereferences null in _Mtx_lock. sentry.dll crashed that way inside DllMain under Proton.
# _DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR restores the old, runtime-initialised constructor. It must reach the compiled
# ports too (spdlog, toml++ hold mutexes in their static libraries), not only our own code (CMakeLists.txt).
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)

set(VCPKG_C_FLAGS "/D_DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR")
set(VCPKG_CXX_FLAGS "/D_DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR")
