# dk2ml/

The plugin API of the **Door Kickers 2 Native Mod Loader** (dk2ml), copied from the loader's `include/` (the loader's
template zip, `DK2-NativeModTemplate`, ships the same files in its `dk2ml\` folder):
- `dk2ml.h`: the C API. Free Camera uses the loader's events (FRAME, MAP_LOADED), the GUI kit (the Esc button) and
  input capture.
- `dk2ml.hpp`: the header-only C++ layer. `src/game/Game.h`/`Game.cpp` declare every game name as a `dk2ml::Fn`/`Field`/
  `Global`/`TypeSize`/`Enum` binding, `dk2ml::ResolveAll` resolves them, and hooks use `dk2ml::Hook` with typed
  arguments (`dk2ml::Arg<T>`).

To update, copy both files from the loader's `include/` over these, rebuild, and dry-run the plugin with the loader's
`symtest.exe` (see CLAUDE.md). The manifest's first argument is the `DK2ML_API_VERSION` the plugin needs.
