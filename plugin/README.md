ekz_vc_plugin — native bridge skeleton

Purpose
-------
This is a minimal skeleton for a native plugin that provides a local HTTP/WebSocket bridge to the browser helper.
It is intended to run on the host machine (or on each client when relevant) and expose simple endpoints:

Endpoints (proposed)
- GET /players  -> JSON list of players with positions: { players: [{id: 1, pos: [x,y,z], yaw: 0, pitch: 0}, ...] }
- GET /mutes    -> JSON of global mutes: { globalMuted: { "1": true, "2": false } }
- POST /mute    -> Accepts { playerId: 2, mute: true } to apply a global mute (host-only)

Design notes
------------
- The plugin provides a C API (`StartPlugin`, `StopPlugin`, `SetGlobalMute`, `GetPlayersJson`) that can be called from the engine's Lua side if the engine/plugin API permits registering functions.
- The plugin should ideally be compiled as a DLL and loaded by Teardown as a native mod/plugin; the exact loading mechanism depends on the game's plugin API and build system.

Build
-----
This skeleton includes a `CMakeLists.txt` which fetches `cpp-httplib` using CMake's `FetchContent`.

Windows (Visual Studio) build steps:
1. Install CMake (>= 3.14) and Visual Studio 2019/2022 with C++ workloads.
2. From the `plugin` folder run:
   mkdir build && cd build
   cmake .. -G "Visual Studio 17 2022"           # or your generator
   cmake --build . --config Release
3. The built DLL will be located in `build/Release` (or appropriate config folder).

Notes:
- The CMake script uses `FetchContent` to retrieve `cpp-httplib` as a header-only dependency so no external package install is required.
- The plugin binds an HTTP server to 127.0.0.1:8765. For security, ensure the host firewall is configured appropriately.

Implementation status
-------------------
- Embedded HTTP endpoints implemented and available on `127.0.0.1:8765` by default: `GET /players`, `GET /mutes`, `GET /events` (long-poll), and `POST /mute`.
- JSON handling (nlohmann/json) provides structured payloads and `lastUpdated` timestamps for `/mutes` and `/players`.
- Optional real-time push via WebSocket/SSE is a possible improvement; currently a blocking long-poll `/events` endpoint is implemented to deliver timely updates to helpers.
- The plugin exposes a C API (`StartPlugin`, `StopPlugin`, `SetGlobalMute`, `GetPlayersJson`, etc.) which can be bound to the engine's native plugin interface if desired; engine integration (automatic binding/callbacks) is environment-specific and remains as an integration step.
- Added a `PluginSetDebug`/`PluginGetDebug` API and gated runtime logs behind the debug flag for quieter default operation.

Security
--------
- Bind to localhost only (127.0.0.1) to avoid exposing controls to the network.
- Optionally protect with a short token or Unix domain socket on supported platforms.

If you want, I can continue and provide a more complete C++ example using a specific library (Boost.Beast or cpp-httplib) and a working CMake project that compiles on Windows.
