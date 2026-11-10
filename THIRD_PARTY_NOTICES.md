# Third-party notices and upstream attribution

SM64 Diorama MR is built on a long chain of community projects. Copyright and
license terms remain with their respective authors.

## Primary upstream projects

- [FullDiveGames SM64 Co-Op DX VR Standalone](https://github.com/fulldivegames/sm64coopdx-VR-Standalone-Physics-based) — native Quest/Android VR foundation.
- [FullDiveGames SM64 Co-Op DX VR](https://github.com/fulldivegames/sm64coopdx-VR-Physics-based) — PC VR and tracked-interaction foundation.
- [SM64 Co-Op DX](https://github.com/coop-deluxe/sm64coopdx) — multiplayer game, mod, Lua, renderer, and platform foundation.
- [sm64ex-coop](https://github.com/djoslin0/sm64ex-coop), sm64ex, and the Super Mario 64 decompilation/PC-port contributors.

The primary upstream repositories above do not currently declare one
project-wide license. See [LICENSE.md](LICENSE.md).

## Components with separate terms

- Lua 5.3.5: MIT-style Lua license in
  `platform/android/third_party/lua-5.3.5/doc/readme.html`.
- miniz: license notice in `src/pc/utils/miniz/LICENSE`.
- IDO compiler tools: `tools/ido5.3_compiler/LICENSE.md`.
- n64graphics_ci tools: `tools/n64graphics_ci_dir/LICENSE`.
- Khronos OpenXR loader for Android: supplied through the declared Maven
  dependency and governed by the upstream Khronos license.
- Discord Game SDK and CoopNet binaries retained from upstream are subject to
  their respective provider/upstream terms and are not relicensed here.

The historical contributor list is retained in [credits.txt](credits.txt).
This summary is not a replacement for license notices contained in individual
files or dependencies.
