#version 2

-- ekzesh's Proximity Voice Chat
-- Main multiplayer-compatible script
-- Features:
--  - Per-player local mute
--  - Host global mute (applies to everyone via shared state)
--  - Pause menu settings UI (accessible via Pause menu button)
--  - Persistent settings via registry (SetString/GetString)
--  - Hooks and documentation placeholders for integrating external audio transport

-- NOTE: Teardown (experimental API) does not provide microphone capture or raw audio streaming APIs.
-- This mod implements the management, UI and sync. Actual microphone capture/transport must be implemented
-- by an external helper/native plugin. See README.md for integration instructions.

local MOD_PREFIX = "ekz_vc"

-- Shared state is synchronized from server -> clients
shared.globalMuted = shared.globalMuted or {}  -- keyed by playerId -> true/false
shared.version = shared.version or 1
shared.release_version = shared.release_version or "1.0.0" -- mod release semantic version

-- Server-only data
server.players = server.players or {}

-- Client-only data
client.localMuted = client.localMuted or {}   -- keyed by playerId -> true/false (client-side preference)
client.settings = client.settings or {range = 20.0, volume = 1.0, enableProximity = true, showHelp = false, reduceMotion = false, debug = false}

-- Utility: simple serialization for persistent storage (k=v;k2=v2)
local function serialize_table(tbl)
    local parts = {}
    for k,v in pairs(tbl) do
        parts[#parts+1] = tostring(k) .. "=" .. tostring(v)
    end
    return table.concat(parts, ";")
end

local function deserialize_table(str)
    local t = {}
    if not str then return t end
    for pair in string.gmatch(str, "[^;]+") do
        local k,v = string.match(pair, "([^=]+)=?(.*)")
        if k then t[k] = v end
    end
    return t
end

-- Simple helper to wrap long text into multiple lines (naive by char count)
local function wrap_text(s, max_chars)
    if not s or s == "" then return {""} end
    max_chars = max_chars or 80
    local out = {}
    local i = 1
    while i <= #s do
        local chunk = string.sub(s, i, i + max_chars - 1)
        out[#out + 1] = chunk
        i = i + max_chars
    end
    return out
end

-- Persistence helpers (registry via SetString/GetString)
local function save_server_state()
    if server then
        SetString(MOD_PREFIX.."_global", serialize_table(shared.globalMuted))
    end
end

local function load_server_state()
    local s = GetString(MOD_PREFIX.."_global", "")
    if s and s ~= "" then
        local t = deserialize_table(s)
        for k,v in pairs(t) do
            shared.globalMuted[tonumber(k) or k] = (v == "true" or v == "1")
        end
    end
end

local function save_client_prefs()
    local name = GetPlayerName(0) or "local"
    SetString(MOD_PREFIX.."_prefs_"..name, serialize_table(client.settings))
    SetString(MOD_PREFIX.."_localmutes_"..name, serialize_table(client.localMuted))
end

local function load_client_prefs()
    local name = GetPlayerName(0) or "local"
    local s = GetString(MOD_PREFIX.."_prefs_"..name, "")
    if s and s ~= "" then
        local t = deserialize_table(s)
        if t.range then client.settings.range = tonumber(t.range) or client.settings.range end
        if t.volume then client.settings.volume = tonumber(t.volume) or client.settings.volume end
        if t.enableProximity then client.settings.enableProximity = (t.enableProximity == "true") end
        if t.showHelp then client.settings.showHelp = (t.showHelp == "true") end
        if t.reduceMotion then client.settings.reduceMotion = (t.reduceMotion == "true") end
        if t.debug then client.settings.debug = (t.debug == "true") end
    end
    local m = GetString(MOD_PREFIX.."_localmutes_"..name, "")
    if m and m ~= "" then
        client.localMuted = deserialize_table(m)
    end
end

-- Logging helper (gated by client.settings.debug)
function client.log(...)
    if not client.settings or not client.settings.debug then return end
    local parts = {}
    for i=1,select("#", ...) do parts[#parts+1] = tostring(select(i, ...)) end
    Log(table.concat(parts, " "))
end

-- Server section
function server.init()
    load_server_state()
    -- sync initial global mutes to plugin if available
    if SetGlobalMute then
        for k,v in pairs(shared.globalMuted) do
            SetGlobalMute(tonumber(k), v and 1 or 0)
        end
    end
end

function server.tick(dt)
    -- Track players for a simple list; react to joins/leaves
    local all = GetAllPlayers()
    -- If any saved global mutes reference invalid players, keep them (they are by id)
end

-- Host-only call: set/unset global mute
function server.setGlobalMute(playerId, mute)
    if not IsPlayerValid(playerId) then return end
    shared.globalMuted[playerId] = mute and true or false
    -- persist
    save_server_state()
    -- notify plugin if available
    if SetGlobalMute then
        SetGlobalMute(playerId, mute and 1 or 0)
    end
end

-- Client section
local ui_open = false
local ui_scroll = 0
local pause_btn_location = "main_bottom"

function client.init()
    load_client_prefs()
    -- Push initial proximity settings to plugin if available
    if SetLocalProximitySettings then SetLocalProximitySettings(client.settings.range, client.settings.volume) end
    -- Ensure plugin knows local mute choices
    for k,v in pairs(client.localMuted) do
        if SetLocalMute and (v == true or v == "true") then SetLocalMute(tonumber(k), 1) end
    end
    -- Apply any existing global mutes to the local plugin
    client._lastAppliedGlobalMutes = client._lastAppliedGlobalMutes or {}
    for k,v in pairs(shared.globalMuted) do
        if SetGlobalMute then SetGlobalMute(tonumber(k), v and 1 or 0); client._lastAppliedGlobalMutes[tonumber(k)] = v end
    end

    -- start periodic players JSON update timer
    client._lastPlayersJsonSend = 0

    -- Register desired plugin event handler name (engine may call this handler directly if it supports callbacks)
    if PluginRegisterEventHandler then
        PluginRegisterEventHandler("onPluginEvent")
    end
    -- Propagate debug setting to native plugin (if present)
    if PluginSetDebug then PluginSetDebug(client.settings.debug and 1 or 0) end

    -- Auto start native WebRTC/audio if plugin is present
    if StartWebRTC then StartWebRTC() end
    if client.settings.enableProximity and StartAudioCapture then StartAudioCapture() end
    client._webrtcPeersInitiated = client._webrtcPeersInitiated or {}
end

function client.tick(dt)
    -- Keep local mute table consistent: remove entries for non-existing players
    for k,v in pairs(client.localMuted) do
        if not IsPlayerValid(tonumber(k)) then client.localMuted[k] = nil end
    end

    -- If any shared global mute changed, obey it by keeping local mute flag for UI

    -- Process plugin events (signaling events produced by native plugin)
    client.processPluginEvents()

    -- Sync global mute state to local plugin (only when it changes)
    client._lastAppliedGlobalMutes = client._lastAppliedGlobalMutes or {}
    for k,v in pairs(shared.globalMuted) do
        local pid = tonumber(k) or k
        if client._lastAppliedGlobalMutes[pid] ~= v then
            if SetGlobalMute then SetGlobalMute(pid, v and 1 or 0) end
            client._lastAppliedGlobalMutes[pid] = v
        end
    end
    -- clear any removed entries
    for pid,_ in pairs(client._lastAppliedGlobalMutes) do
        if not shared.globalMuted[pid] then
            if SetGlobalMute then SetGlobalMute(pid, 0) end
            client._lastAppliedGlobalMutes[pid] = nil
        end
    end

    -- Auto-init peer offers for new players (one-time per seen player)
    local all = GetAllPlayers()
    client._webrtcPeersInitiated = client._webrtcPeersInitiated or {}
    local myId = 0
    if GetPlayerId then myId = GetPlayerId(0) end
    local seen = {}
    for i=1,#all do
        local pid = all[i]
        if pid ~= myId then
            seen[pid] = true
            if not client._webrtcPeersInitiated[pid] then
                client._webrtcPeersInitiated[pid] = true
                if CreateOfferForPlayer then
                    CreateOfferForPlayer(pid)
                end
            end
        end
    end
    -- remove departed players from tracking
    for pid,_ in pairs(client._webrtcPeersInitiated) do
        if not seen[pid] then client._webrtcPeersInitiated[pid] = nil end
    end

    -- Ensure audio capture follows the enableProximity toggle
    if client.settings.enableProximity and StartAudioCapture and not client._captureStarted then
        StartAudioCapture()
        client._captureStarted = true
    elseif (not client.settings.enableProximity) and client._captureStarted and StopAudioCapture then
        StopAudioCapture()
        client._captureStarted = false
    end

    -- Periodically send players transforms to plugin (throttled to 1Hz)
    client._lastPlayersJsonSend = (client._lastPlayersJsonSend or 0) + dt
    if client._lastPlayersJsonSend >= 1.0 then
        client._lastPlayersJsonSend = 0
        client.sendPlayersJson()
    end

    -- Update notifications TTL
    if client.notifications then
        local i = 1
        while i <= #client.notifications do
            local n = client.notifications[i]
            n.ttl = (n.ttl or 0) - dt
            if n.ttl <= 0 then table.remove(client.notifications, i) else i = i + 1 end
        end
    end

    -- Update help tip TTL
    if client.helpTip then
        client.helpTip.ttl = (client.helpTip.ttl or 0) - dt
        if client.helpTip.ttl <= 0 then client.helpTip = nil end
    end

    -- Time accumulator used for subtle UI animations (pulsing)
    client._time = (client._time or 0) + dt

    -- Handle mute test timer (host-initiated)
    if client._muteTestTimer and client._muteTestTimer > 0 then
        client._muteTestTimer = client._muteTestTimer - dt
        if client._muteTestTimer <= 0 and client._muteTestTarget then
            ServerCall("server.setGlobalMute", client._muteTestTarget, 0)
            AddNotification("Unmuted "..(GetPlayerName(client._muteTestTarget) or ("Player "..client._muteTestTarget)), 3)
            client._muteTestTarget = nil
            client._muteTestTimer = nil
        end
    end

    -- Handle non-blocking E2E test state machine
    if client._e2eTest then
        local t = client._e2eTest
        t.timer = (t.timer or 0) - dt
        local pid = t.target
        local ps = client.peerStatus and client.peerStatus[pid]

        -- If we observed a block or forced disconnect, request unmute to verify restoration
        if (not t.observed) and ps and (ps.blocked == true or ps.connected == false) then
            t.observed = true
            AddNotification("E2E: observed block/forced-disconnect for "..(GetPlayerName(pid) or ("Player "..pid)), 3)
            ServerCall("server.setGlobalMute", pid, 0)
            t.unmuteRequested = true
            t.timer = 3.0 -- short window to observe unmute
        end

        -- If timer expired, finalize and record result
        if t.timer <= 0 then
            local finalPs = client.peerStatus and client.peerStatus[pid]
            local passed = false
            if t.observed and t.unmuteRequested then
                if finalPs and (finalPs.globalMuted == false and (finalPs.blocked ~= true)) then passed = true end
            end
            local msg = passed and ("E2E MUTE TEST PASS for "..(GetPlayerName(pid) or ("Player "..pid))) or ("E2E MUTE TEST FAIL for "..(GetPlayerName(pid) or ("Player "..pid)))
            AddNotification(msg, 5)
            client._e2eTestResults = client._e2eTestResults or {}
            table.insert(client._e2eTestResults, {target = pid, pass = passed, time = os.time()})
            client.log(msg)
            client._e2eTest = nil
        end
    end

    -- Keyboard shortcuts (host-only): Ctrl+M -> Run mute test, Ctrl+E -> Run E2E mute test
    if InputPressed and InputPressed('m') and (IsKeyDown and (IsKeyDown('lctrl') or IsKeyDown('rctrl'))) then
        if IsPlayerHost() then client.runMuteTest() else AddNotification('Only host can run mute test', 3) end
    end
    if InputPressed and InputPressed('e') and (IsKeyDown and (IsKeyDown('lctrl') or IsKeyDown('rctrl'))) then
        if IsPlayerHost() then client.runMuteE2ETest() else AddNotification('Only host can run E2E mute test', 3) end
    end
end

function client.sendPlayersJson()
    local players = GetAllPlayers()
    local parts = {}
    parts[#parts+1] = '{"players":['
    for i=1,#players do
        local pid = players[i]
        local pname = GetPlayerName(pid) or ("Player "..pid)
        local x,y,z = 0,0,0
        -- try common APIs for position
        if GetPlayerTransform then
            local t = GetPlayerTransform(pid)
            if t and t.pos then x,y,z = t.pos[1], t.pos[2], t.pos[3] end
        elseif GetPlayerPos then
            local px,py,pz = GetPlayerPos(pid)
            if px then x,y,z = px,py,pz end
        end
        local entry = string.format('{"id":%d,"name":"%s","pos":[%.3f,%.3f,%.3f],"yaw":0,"pitch":0}', pid, pname:gsub('"','\"'), x or 0.0, y or 0.0, z or 0.0)
        parts[#parts+1] = entry
        if i < #players then parts[#parts+1] = ',' end
    end
    parts[#parts+1] = '],"localId":'..tostring(GetPlayerId and GetPlayerId(0) or 0)..'}'
    local json = table.concat(parts)
    if SetPlayersJson then SetPlayersJson(json) end
end

-- Base64 decode helper for parsing plugin event payloads
local b='ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/'
local function b64dec(data)
    data = string.gsub(data, '[^'..b..'=]', '')
    return (data:gsub('.', function(x)
        if x == '=' then return '' end
        local r,f='',(b:find(x)-1)
        for i=6,1,-1 do r=r..(math.floor(f/2^(i-1))%2) end
        return r
    end):gsub('%d%d%d%d%d%d%d%d', function(x)
        return string.char(tonumber(x,2))
    end))
end

function client.processPluginEvents()
    local function handle_event(ev)
        if not ev or ev == "" then return end
        local parts = {}
        for part in string.gmatch(ev, '[^|]+') do table.insert(parts, part) end
        local t = parts[1]
        if t == 'local-sdp' then
            local targetId = tonumber(parts[2])
            local b64 = parts[3]
            local sdpType = parts[4] or "offer"
            local sdp = b64dec(b64)
            -- escape sdp for JSON
            sdp = sdp:gsub('\\', '\\\\'):gsub('"', '\\"'):gsub('\n','\\n')
            local json = '{"type":"sdp","sdp":"'..sdp..'","sdpType":"'..sdpType..'"}'
            local myPid = 0
            if GetPlayerId then myPid = GetPlayerId(0) end
            ServerCall('server.relaySignal', targetId, myPid, json)
        elseif t == 'candidate' then
            local targetId = tonumber(parts[2])
            local b64 = parts[3]
            local cand = b64dec(b64)
            cand = cand:gsub('\\', '\\\\'):gsub('"', '\\"'):gsub('\n','\\n')
            local json = '{"type":"candidate","candidate":"'..cand..'"}'
            local myPid = 0
            if GetPlayerId then myPid = GetPlayerId(0) end
            ServerCall('server.relaySignal', targetId, myPid, json)
        elseif t == 'peer-open' then
            local pid = tonumber(parts[2])
            client.peerStatus = client.peerStatus or {}
            client.peerStatus[pid] = client.peerStatus[pid] or {}
            client.peerStatus[pid].connected = true
            client.peerStatus[pid].blocked = false
        elseif t == 'peer-closed' then
            local pid = tonumber(parts[2])
            client.peerStatus = client.peerStatus or {}
            client.peerStatus[pid] = client.peerStatus[pid] or {}
            client.peerStatus[pid].connected = false
        elseif t == 'level' then
            local pid = tonumber(parts[2])
            local level = tonumber(parts[3]) or 0
            client.peerStatus = client.peerStatus or {}
            client.peerStatus[pid] = client.peerStatus[pid] or {}
            client.peerStatus[pid].level = level
        elseif t == 'global-mute' then
            local pid = tonumber(parts[2])
            local val = tonumber(parts[3]) or 0
            client.peerStatus = client.peerStatus or {}
            client.peerStatus[pid] = client.peerStatus[pid] or {}
            client.peerStatus[pid].globalMuted = (val == 1)
            local myPid = GetPlayerId and GetPlayerId(0) or 0
            local name = GetPlayerName(pid) or ("Player "..tostring(pid))
            if val == 1 then
                if pid == myPid then AddNotification("You were globally muted by the host", 4) else AddNotification(name.." was globally muted", 3) end
            else
                if pid == myPid then AddNotification("You were unmuted by the host", 3) else AddNotification(name.." was unmuted", 2) end
            end
        elseif t == 'blocked' then
            local pid = tonumber(parts[2])
            client.peerStatus = client.peerStatus or {}
            client.peerStatus[pid] = client.peerStatus[pid] or {}
            client.peerStatus[pid].blocked = true
            local name = GetPlayerName(pid) or ("Player "..tostring(pid))
            AddNotification("Offer blocked: "..name.." (global mute)", 4)
        end
    end

    if PluginHasEvent then
        while PluginHasEvent() ~= 0 do
            local ev = PluginPopEvent()
            if not ev or ev == "" then break end
            handle_event(ev)
        end
        return
    end

    while true do
        local ev = PluginPopEvent()
        if not ev or ev == "" then break end
        handle_event(ev)
    end
end

-- Simple in-game notification system (TTL in seconds)
client.notifications = client.notifications or {}
function AddNotification(msg, duration)
    duration = duration or 3
    client.notifications = client.notifications or {}
    table.insert(client.notifications, {msg = msg, ttl = duration, max_ttl = duration})
end

-- Compact contextual help tip (shown in settings panel when clicked)
client.helpTip = client.helpTip or nil
function ShowHelpTip(msg, duration, source)
    duration = duration or 4
    client.helpTip = { msg = msg, ttl = duration, max_ttl = duration, source = source }
end

function ClearHelpTip()
    client.helpTip = nil
end

-- Test mute harness (host only) — mutes first other player for N seconds
function client.runMuteTest()
    if not IsPlayerHost() then
        AddNotification("Only host can run mute test", 3)
        return
    end
    local all = GetAllPlayers()
    local me = GetPlayerId and GetPlayerId(0) or 0
    local target = nil
    for i=1,#all do
        if all[i] ~= me then target = all[i]; break end
    end
    if not target then AddNotification("No other players to test", 3); return end
    ServerCall("server.setGlobalMute", target, 1)
    client._muteTestTarget = target
    client._muteTestTimer = 5.0
    AddNotification("Muted "..(GetPlayerName(target) or ("Player "..target)).." for 5s (test)", 3)
end

-- Automated non-blocking E2E mute test harness (host-only)
function client.runMuteE2ETest()
    if not IsPlayerHost() then
        AddNotification("Only host can run E2E mute test", 3)
        return
    end
    local all = GetAllPlayers()
    local me = GetPlayerId and GetPlayerId(0) or 0
    local target = nil
    for i=1,#all do
        if all[i] ~= me then target = all[i]; break end
    end
    if not target then AddNotification("No other players to test", 3); return end

    client._e2eTest = { target = target, timer = 6.0, observed = false, unmuteRequested = false }
    -- trigger mute and monitor events/state in client.tick
    ServerCall("server.setGlobalMute", target, 1)
    AddNotification("E2E mute test: muting "..(GetPlayerName(target) or ("Player "..target)), 3)
end

-- Server relay for signals: forwards to target client
function server.relaySignal(toPlayerId, fromPlayerId, json)
    -- call client handler on the destination client
    ClientCall('client.handleRemoteSignal', toPlayerId, fromPlayerId, json)
end

-- Client handler for incoming signals from other players
function client.handleRemoteSignal(fromPlayerId, json)
    client.log('received remote signal from '..tostring(fromPlayerId)..': '..tostring(json))
    if HandleSignalFromPlayer then
        HandleSignalFromPlayer(fromPlayerId, json)
    else
        client.log('plugin HandleSignalFromPlayer not available')
    end
end

function client.draw()
    -- Add button to Pause menu
    if PauseMenuButton("Proximity Voice Chat", pause_btn_location) then
        ui_open = not ui_open
    end

    -- Draw notifications overlay (centered top, adaptive width, with muted background and fade)
    if client.notifications and #client.notifications > 0 then
        UiMakeInteractive()
        UiPush()
            local count = #client.notifications
            -- approximate width by longest message length (chars) * char_px, clamp to sensible range
            local maxChars = 0
            for i=1,count do maxChars = math.max(maxChars, #client.notifications[i].msg) end
            local char_px = 8 -- approximate per character width
            local width = math.min(math.max(320, maxChars * char_px + 40), 900)
            local height = 28 + (count-1) * 26
            -- position above the pause menu; if menu is open, shift up a bit
            local baseY = 120
            if ui_open then baseY = 80 end
            UiTranslate(UiCenter() - (width/2), baseY)

            -- background
            UiColor(0,0,0,0.75)
            UiRect(width, height)
            UiColor(1,1,1,1)

            -- messages
            UiTranslate(12, 6)
            UiFont("regular.ttf", 18)
            for i=1,count do
                local n = client.notifications[i]
                local alpha = math.max(0, math.min(1, (n.ttl or 0) / (n.max_ttl or 1)))
                UiColor(1,1,1,alpha)
                UiText(n.msg)
                UiTranslate(0, 22)
            end
            UiColor(1,1,1,1)
        UiPop()
    end

    if ui_open then
        UiMakeInteractive()
        UiPush()
            UiTranslate(UiCenter(), 200)
            UiFont("bold.ttf", 28)
            UiText("ekzesh's Proximity Voice Chat Settings")
            UiTranslate(0, 20)
            UiFont("regular.ttf", 18)
            UiText("Range (meters)")
            UiTranslate(0,10)
            -- simple +/- buttons for range
            if UiTextButton("-", 40, 30) then client.settings.range = math.max(1, client.settings.range - 1); if SetLocalProximitySettings then SetLocalProximitySettings(client.settings.range, client.settings.volume) end end
            UiTranslate(50, -30)
            UiText(tostring(math.floor(client.settings.range)))
            UiTranslate(50, 0)
            if UiTextButton("+", 40, 30) then client.settings.range = client.settings.range + 1; if SetLocalProximitySettings then SetLocalProximitySettings(client.settings.range, client.settings.volume) end end

            UiTranslate(-100, 40)
            UiText("Volume")
            UiTranslate(0, 10)
            if UiTextButton("-", 40, 30) then client.settings.volume = math.max(0, client.settings.volume - 0.1); if SetLocalProximitySettings then SetLocalProximitySettings(client.settings.range, client.settings.volume) end end
            UiTranslate(50, -30)
            UiText(string.format("%.2f", client.settings.volume))
            UiTranslate(50, 0)
            if UiTextButton("+", 40, 30) then client.settings.volume = math.min(2, client.settings.volume + 0.1); if SetLocalProximitySettings then SetLocalProximitySettings(client.settings.range, client.settings.volume) end end

            UiTranslate(-100, 40)
            local label = client.settings.enableProximity and "Enable: ON" or "Enable: OFF"
            if UiTextButton(label, 200, 30) then client.settings.enableProximity = not client.settings.enableProximity end
            UiTranslate(206, -30)
            local isActive = client.helpTip and client.helpTip.source == "enable"
            local pulse = 1.0
            if isActive and not client.settings.reduceMotion then pulse = 0.7 + 0.3 * (math.sin((client._time or 0) * 6) * 0.5 + 0.5) end
            UiColor(0.2 * pulse, 0.6 * pulse, 1 * pulse, 1)
            if UiTextButton("?", 24, 24) then ShowHelpTip("Enable: When ON the plugin will start audio capture and proximity mixing. Turn OFF to stop capture.", 5, "enable") end
            UiColor(1,1,1,1)
            UiTranslate(-206, 30)

            UiTranslate(220, 0)
            local helpLabel = client.settings.showHelp and "Help: ON" or "Help: OFF"
            if UiTextButton(helpLabel, 160, 30) then client.settings.showHelp = not client.settings.showHelp end
            UiTranslate(166, -30)
            local isHelpActive = client.helpTip and client.helpTip.source == "help_toggle"
            local pulse2 = 1.0
            if isHelpActive and not client.settings.reduceMotion then pulse2 = 0.7 + 0.3 * (math.sin((client._time or 0) * 6) * 0.5 + 0.5) end
            UiColor(0.2 * pulse2, 0.6 * pulse2, 1 * pulse2, 1)
            if UiTextButton("?", 24, 24) then ShowHelpTip("Help: Toggle contextual help text visible in this menu.", 4, "help_toggle") end
            UiColor(1,1,1,1)
            UiTranslate(-166, 30)

            -- Reduce motion toggle (accessibility)
            UiTranslate(0, 16)
            local rmLabel = client.settings.reduceMotion and "Reduce motion: ON" or "Reduce motion: OFF"
            if UiTextButton(rmLabel, 220, 28) then client.settings.reduceMotion = not client.settings.reduceMotion end
            UiTranslate(226, -28)
            local isReduceActive = client.helpTip and client.helpTip.source == "reduce_motion"
            local rmPulseUI = 1.0
            if isReduceActive and not client.settings.reduceMotion then rmPulseUI = 0.7 + 0.3 * (math.sin((client._time or 0) * 6) * 0.5 + 0.5) end
            UiColor(0.2 * rmPulseUI, 0.6 * rmPulseUI, 1 * rmPulseUI, 1)
            if UiTextButton("?", 24, 24) then ShowHelpTip("Reduce motion: disable pulsing animations for accessibility.", 5, "reduce_motion") end
            UiColor(1,1,1,1)
            UiTranslate(-226, 28)

            -- Debug logs toggle (development only)
            UiTranslate(0, 16)
            local dbgLabel = client.settings.debug and "Debug logs: ON" or "Debug logs: OFF"
            if UiTextButton(dbgLabel, 220, 28) then
                client.settings.debug = not client.settings.debug
                if PluginSetDebug then PluginSetDebug(client.settings.debug and 1 or 0) end
            end
            UiTranslate(226, -28)
            local isDbgActive = client.helpTip and client.helpTip.source == "debug"
            local dbgPulseUI = 1.0
            if isDbgActive and not client.settings.reduceMotion then dbgPulseUI = 0.7 + 0.3 * (math.sin((client._time or 0) * 6) * 0.5 + 0.5) end
            UiColor(0.2 * dbgPulseUI, 0.6 * dbgPulseUI, 1 * dbgPulseUI, 1)
            if UiTextButton("?", 24, 24) then ShowHelpTip("Debug: enable verbose console logging for the mod (development use). Disable for normal gameplay.", 6, "debug") end
            UiColor(1,1,1,1)
            UiTranslate(-226, 28)

            UiTranslate(-220, 40)

            UiTranslate(-200, 60)
            UiText("Players:")
            UiTranslate(0, 10)

            -- Help block (wrapped for readability)
            if client.settings.showHelp then
                UiFont("regular.ttf", 14)
                UiColor(0.9,0.9,0.9,1)
                local helpLines = wrap_text("Help: Local mute hides a player's audio only for you. Global mute (host) mutes the player for everyone and may forcibly disconnect their voice connection.", 72)
                for i=1,#helpLines do UiText(helpLines[i]); UiTranslate(0, 18) end
                UiColor(1,1,1,1)
                UiTranslate(0, 4)
            end

            -- List players with mute toggles, show global mute for host (clean column layout)
            local players = GetAllPlayers()
            local rowH = 36
            local panelW = 640
            UiFont("regular.ttf", 16)
            for idx=1,#players do
                local pid = players[idx]
                local pname = GetPlayerName(pid) or ("Player "..pid)
                -- row background for clarity
                UiPush()
                    UiTranslate(0, (idx-1) * rowH - ui_scroll)
                    UiColor(1,1,1,0.03)
                    UiRect(panelW - 40, rowH - 6)
                    UiColor(1,1,1,1)
                    UiTranslate(8, 6)

                    -- Name column (clamped)
                    local displayName = pname
                    if #displayName > 20 then displayName = string.sub(displayName,1,17) .. "..." end
                    UiFont("regular.ttf", 16)
                    UiText(displayName)

                    -- Status column (right of name)
                    UiTranslate(240, 0)
                    local ps = client.peerStatus and client.peerStatus[pid]
                    if ps and ps.globalMuted then
                        UiFont("regular.ttf", 14)
                        UiColor(1,0.6,0.2,1)
                        UiText("(GLOBAL MUTED)")
                        UiColor(1,1,1,1)
                    elseif ps and ps.blocked then
                        UiFont("regular.ttf", 14)
                        UiColor(1,0.2,0.2,1)
                        UiText("(BLOCKED)")
                        UiColor(1,1,1,1)
                    else
                        local connText = ps and (ps.connected and "connected" or "disconnected") or "disconnected"
                        UiFont("regular.ttf", 14)
                        UiText(connText)
                    end

                    -- Level bar column
                    UiTranslate(110, 0)
                    local lvl = ps and ps.level or 0
                    local blocks = math.floor((lvl / 100) * 10)
                    local bar = "["
                    for j=1,10 do bar = bar .. (j <= blocks and "#" or " ") end
                    bar = bar .. "]"
                    UiText(bar)

                    -- Local mute button
                    UiTranslate(120, -2)
                    local localMuted = client.localMuted[pid] == "true" or client.localMuted[pid] == true
                    local txt = localMuted and "Unmute (local)" or "Mute (local)"
                    if UiTextButton(txt, 140, 28) then
                        client.localMuted[pid] = not localMuted and true or nil
                        save_client_prefs()
                        if SetLocalMute then
                            if client.localMuted[pid] then SetLocalMute(pid, 1) else SetLocalMute(pid, 0) end
                        end
                    end
                    UiTranslate(146, -2)
                    local isLocalActive = client.helpTip and client.helpTip.source == ("local_mute_"..tostring(pid))
                    local lpulse = 1.0
                    if isLocalActive and not client.settings.reduceMotion then lpulse = 0.7 + 0.3 * (math.sin((client._time or 0) * 6) * 0.5 + 0.5) end
                    UiColor(0.2 * lpulse, 0.6 * lpulse, 1 * lpulse, 1)
                    if UiTextButton("?", 24, 24) then ShowHelpTip("Local mute hides a player's audio only for you.", 4, "local_mute_"..tostring(pid)) end
                    UiColor(1,1,1,1)
                    UiTranslate(-146, 2)

                    -- Global mute (host only)
                    UiTranslate(260, 0)
                    if IsPlayerHost() then
                        local globalMuted = shared.globalMuted[pid] == true
                        local gtxt = globalMuted and "Unmute (global)" or "Mute (global)"
                        if UiTextButton(gtxt, 120, 28) then
                            ServerCall("server.setGlobalMute", pid, not globalMuted)
                        end
                        UiTranslate(126, 0)
                        local isGlobalActive = client.helpTip and client.helpTip.source == ("global_mute_"..tostring(pid))
                        local gpulse = 1.0
                        if isGlobalActive and not client.settings.reduceMotion then gpulse = 0.7 + 0.3 * (math.sin((client._time or 0) * 6) * 0.5 + 0.5) end
                        UiColor(0.2 * gpulse, 0.6 * gpulse, 1 * gpulse, 1)
                        if UiTextButton("?", 24, 24) then ShowHelpTip("Global mute: mutes this player for everyone and may disconnect their voice connection.", 5, "global_mute_"..tostring(pid)) end
                        UiColor(1,1,1,1)
                        UiTranslate(-126, 0)
                    end                    UiTranslate(140, 0)
                    if IsPlayerHost() then
                        local globalMuted = shared.globalMuted[pid] == true
                        local gtxt = globalMuted and "Unmute (global)" or "Mute (global)"
                        if UiTextButton(gtxt, 120, 28) then
                            ServerCall("server.setGlobalMute", pid, not globalMuted)
                        end
                        UiTranslate(126, 0)
                        local isGlobalActive = client.helpTip and client.helpTip.source == ("global_mute_"..tostring(pid))
                        local gpulse = 1.0
                        if isGlobalActive then gpulse = 0.7 + 0.3 * (math.sin((client._time or 0) * 6) * 0.5 + 0.5) end
                        UiColor(0.2 * gpulse, 0.6 * gpulse, 1 * gpulse, 1)
                        if UiTextButton("?", 24, 24) then ShowHelpTip("Global mute: mutes this player for everyone and may disconnect their voice connection.", 5, "global_mute_"..tostring(pid)) end
                        UiColor(1,1,1,1)
                        UiTranslate(-126, 0)
                    end
                UiPop()
            end

            UiTranslate(0, 250)
            if IsPlayerHost() then
                if UiTextButton("Run mute test (Ctrl+M)", 220, 36) then client.runMuteTest() end
                UiTranslate(210, -36)
                local isRMActive = client.helpTip and client.helpTip.source == "run_mute"
                local rmpulse = 1.0
                if isRMActive and not client.settings.reduceMotion then rmpulse = 0.7 + 0.3 * (math.sin((client._time or 0) * 6) * 0.5 + 0.5) end
                UiColor(0.2 * rmpulse, 0.6 * rmpulse, 1 * rmpulse, 1)
                if UiTextButton("?", 24, 24) then ShowHelpTip("Quick host-only mute for 5s used for manual verification. Shortcut: Ctrl+M", 5, "run_mute") end
                UiColor(1,1,1,1)
                UiTranslate(-210, 36)
                UiTranslate(220, 0)
            end
            UiTranslate(0, 40)
            if IsPlayerHost() then
                if UiTextButton("Run E2E mute test (Ctrl+E)", 220, 36) then client.runMuteE2ETest() end
                UiTranslate(210, -36)
                local isREActive = client.helpTip and client.helpTip.source == "run_e2e"
                local repulse = 1.0
                if isREActive and not client.settings.reduceMotion then repulse = 0.7 + 0.3 * (math.sin((client._time or 0) * 6) * 0.5 + 0.5) end
                UiColor(0.2 * repulse, 0.6 * repulse, 1 * repulse, 1)
                if UiTextButton("?", 24, 24) then ShowHelpTip("Automated non-blocking test for blocked/forced-disconnect and unmute restore. Shortcut: Ctrl+E", 5, "run_e2e") end
                UiColor(1,1,1,1)
                UiTranslate(-210, 36)
                UiTranslate(220, 0)
            end
            UiTranslate(0, 40)
            if IsPlayerHost() then
                if UiTextButton("Run E2E mute test (Ctrl+E)", 220, 36) then client.runMuteE2ETest() end
                UiTranslate(210, -36)
                UiColor(0.2,0.6,1,1)
                if UiTextButton("?", 24, 24) then ShowHelpTip("Automated non-blocking test for blocked/forced-disconnect and unmute restore. Shortcut: Ctrl+E", 5) end
                UiColor(1,1,1,1)
                UiTranslate(-210, 36)
                UiTranslate(220, 0)
            end
            if UiTextButton("Save settings", 200, 36) then
                save_client_prefs()
            end
            UiTranslate(220, 0)
            if UiTextButton("Close", 100, 36) then ui_open = false end

            -- Contextual help tip area (small, non-intrusive)
            if client.helpTip then
                UiTranslate(-220, 72)
                UiFont("regular.ttf", 14)
                UiColor(0.1,0.1,0.1,0.9)
                UiRect(440, 48)
                UiColor(1,1,1,1)
                UiTranslate(8, 8)
                UiText(client.helpTip.msg)
            end

            -- Show contextual help for test buttons
            if client.settings.showHelp then
                UiTranslate(-200, 40)
                UiFont("regular.ttf", 14)
                UiColor(0.9,0.9,0.9,1)
                UiText("Run mute test: quick host-only 5s mute for manual verification.")
                UiTranslate(0, 18)
                UiText("Run E2E mute test: automated non-blocking check for blocked/forced-disconnect and unmute restore.")
                UiColor(1,1,1,1)
            end

            -- Show last E2E test result (right-aligned, compact)
            UiTranslate(0, 86)
            UiAlign("right","top")
            UiFont("regular.ttf", 14)
            UiText("Last E2E Test:")
            UiTranslate(0, 18)
            if client._e2eTestResults and #client._e2eTestResults > 0 then
                local r = client._e2eTestResults[#client._e2eTestResults]
                local name = GetPlayerName(r.target) or ("Player "..r.target)
                local res = r.pass and "PASS" or "FAIL"
                UiText(string.format("%s - %s", name, res))
            else
                UiText("(none)")
            end
            UiAlign("center","top")
            UiTranslate(0, -86)
        UiPop()
    end
end

-- Helper to check if a player is effectively muted for this client
function client.isPlayerMutedForLocal(playerId)
    if not IsPlayerValid(playerId) then return true end
    -- Host global mute applies for everyone
    if shared.globalMuted[playerId] then return true end
    -- Local mute preference
    if client.localMuted[playerId] == true or client.localMuted[playerId] == "true" then return true end
    return false
end

-- Exposed API: client can call this to play a short voice sample (for testing)
-- In real integration, external helper should play voice locally according to proximity and mute lists
function client.playTestSampleFrom(playerId)
    -- if muted, skip
    if client.isPlayerMutedForLocal(playerId) then return end
    -- Play a UI sound for demo (replace with real voice playback in integration)
    UiSound("click.ogg")
end

-- Convenience: host global-mute toggle call from client
function client.requestGlobalMute(playerId, mute)
    ServerCall("server.setGlobalMute", playerId, mute)
end

-- Helpers to access E2E test results (for automation)
function client.getLastE2EResult()
    if client._e2eTestResults and #client._e2eTestResults > 0 then return client._e2eTestResults[#client._e2eTestResults] end
    return nil
end

function client.clearE2EResults()
    client._e2eTestResults = nil
end

-- Save on client destroy
function client.destroy()
    save_client_prefs()
    -- stop native audio if running
    if StopAudioCapture then StopAudioCapture() end
    if StopWebRTC then StopWebRTC() end
end

-- Save on server destroy
function server.destroy()
    save_server_state()
end

-- Notes and placeholders:
-- 1) There is no microphone capture or raw streaming API in Teardown's experimental Lua API.
--    The recommended approach for "real" voice is to write a small native extension or external app
--    to capture microphone, do encoding (Opus/OGG), and transport (WebRTC or UDP). That helper should
--    implement its own networking and playback; this mod will provide the mute lists and positional preferences
--    which the helper can use to implement proximity attenuation locally.
-- 2) This script purposely keeps the voice transport out of engine code to avoid unsafe hacks.
-- 3) README includes integration guidance and a suggested event scheme for external helpers.
