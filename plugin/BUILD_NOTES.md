Quick build notes (Windows)

Prereqs:
- CMake >= 3.14
- Visual Studio 2019/2022 (Desktop dev with C++)

Steps (from plugin folder):
mkdir build
cd build
cmake .. -G "Visual Studio 17 2022"  # adjust generator to your VS version
cmake --build . --config Release

Test:
- Run the built DLL with the game or a test runner. The plugin will attempt to bind 127.0.0.1:8765. If binding fails, choose another port and update helper/plugin-bridge.js accordingly.

Notes:
- FetchContent downloads cpp-httplib at configure-time; an internet connection is required for the first configure.
- For a fully production-ready plugin you should add robust error handling, logging and JSON parsing (nlohmann/json recommended).

WebRTC / native media notes:
- This plugin now integrates with libdatachannel (WebRTC stack). CMake will fetch libdatachannel during configure. Building libdatachannel requires a C++ toolchain and may fetch additional dependencies (usrsctp, libjuice/libnice, libsrtp, libopus).
- On Windows you should have the Windows SDK and the Visual Studio C++ toolset installed. The first configure may take a while as libdatachannel and its deps are downloaded and built.
- This plugin also uses PortAudio and libopus for native audio capture, encoding and playback. These are fetched and built by CMake during the first configure.
- The plugin now includes an Opus decoder and a simple PortAudio-based playback thread so incoming encoded packets can be played locally using `HandleIncomingOpusPacket`.
- Peer-to-peer transport: the plugin uses libdatachannel to create PeerConnections and a binary datachannel named `audio` per player. Use `CreateOfferForPlayer(playerId)` to create an SDP offer and `GetLocalSdpForPlayer(playerId)` to retrieve the SDP string for relaying to the remote peer via game signaling. Remote answers/candidates should be passed back with `HandleSignalFromPlayer(playerId, json)`.
- If you plan to distribute binaries, consider building and packaging libdatachannel, PortAudio, and libopus externally and linking prebuilt libraries to speed CI builds and reduce build-time failures.
- You may wish to install vcpkg and add prebuilt packages instead of relying on FetchContent for faster local iteration.
