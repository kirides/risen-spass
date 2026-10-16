-- EXAMPLE 10: spoken dialogue lines (custom voice files, per language).
--
-- What you learn
--   * A Say line of risen.dialog.create can be voiced: give it audio (ogg/wav/mp3, any sample rate, mono or stereo) and the NPC speaks it while the
--     subtitle shows. The line lasts as long as the audio (a line without audio waits a few seconds, depending on the length of an internal name).
--   * Four ways to name the audio of a line:
--       1. the directory convention: voice/<language>/<info name>_<line number>.<ext>  (voice/English/EX_Voice_Greet_2.wav). Language folders are the
--          names the game uses for its voice languages (English, German, French, Russian; codes like en/de work too); a file directly in voice/ is the
--          default for every language. Call risen.voice.load_dir("voice") once, before the dialogues are created.
--       2. `sound = "sounds/farewell.ogg"` on the line: one file for every language.
--       3. `sound = { en = ..., de = ..., default = ... }`: one file per language; a value is a file name of the mod or { data = <bytes> } (audio your
--          Lua code made itself, here a synthesised tone: a mod needs to ship no copyrighted audio to test this).
--       4. a { command = "SaySound", Speaker = "npc", sound = "..." } line: sound only, no subtitle; the language is chosen when the dialogue is created.
--   * Which variant plays: the game's voice language (Options > Language, config key VoiceLanguage), else `default`, else English, else the first
--     one by name. risen.voice.language() tells the current one. Subtitles are separate: `text = { default = "...", de = "..." }` per language.
--   * The layer compiles the files into the engine's own sound format the first time (below <Risen>\bin\spass\generated\example_voice\voice); the same audio is not
--     written twice, and a file that is not usable is refused with nil, reason before anything is created.
--   * risen.voice.say(npc, key_or_sound) makes an NPC speak without a dialogue (a "bark"); risen.voice.speaking(npc) tells whether a sound is playing.
--   * Same as every other runtime content: dialogues are created on load.before AND session.start, the NPC is a risen.npc.persistent (kept across loads).
--
-- Run the live check (about a minute each, English and then German voices; see docs/EXAMPLES.md):
--   .\scripts\test-run.ps1 -Instance 2 -Mods example_voice -WaitForResult -Seconds 240 -Scenario ex_voice -ShowChecks
--   .\scripts\test-run.ps1 -Instance 2 -Mods example_voice -WaitForResult -Seconds 240 -Scenario ex_voice -VoiceLanguage German -Param lang=German -ShowChecks

local NPC = "EX_Voice_Npc"

-- ---- a tone as a WAV file, made by Lua (16 bit mono, 22.05 kHz) ------------------------------------------------------------------
local function le(n, bytes)
    local t = {}
    for i = 1, bytes do
        t[i] = string.char(n % 256)
        n = math.floor(n / 256)
    end
    return table.concat(t)
end

local function tone_wav(freq, seconds)
    local rate = 22050
    local n = math.floor(rate * seconds)
    local samples = {}
    for i = 0, n - 1 do
        local env = math.min(1, i / 200, (n - i) / 400)               -- short fade in and out: no clicks
        local v = math.floor(12000 * env * math.sin(2 * math.pi * freq * i / rate))
        samples[#samples + 1] = le(v < 0 and v + 65536 or v, 2)
    end
    local data = table.concat(samples)
    return "RIFF" .. le(36 + #data, 4) .. "WAVEfmt " .. le(16, 4) .. le(1, 2) .. le(1, 2) .. le(rate, 4) .. le(rate * 2, 4) .. le(2, 2) .. le(16, 2)
        .. "data" .. le(#data, 4) .. data
end

-- ---- the dialogue options ---------------------------------------------------------------------------------------------------------
local function define_content()
    -- the files of voice/ become voice entries keyed by text id: voice/English/EX_Voice_Greet_2.wav answers the text key "EX_Voice_Greet_2"
    -- (risen.dialog.create names the text of line n of an option "<option name>_<n>")
    local keys, files = risen.voice.load_dir("voice")
    if not keys then risen.log.warn("example_voice: " .. tostring(files)) end

    -- 1. voiced by the directory convention (line 2 -> voice/<language>/EX_Voice_Greet_2.wav); the first line has no voice and takes its default time
    risen.dialog.create({
        name = "EX_Voice_Greet", owner = NPC, permanent = true,
        lines = {
            { speaker = "player", listener = "npc", text = { default = "Hello there!", de = "Hallo!" } },
            { speaker = "npc", listener = "player", text = { default = "Welcome, traveller. Can you hear me?", de = "Willkommen, Reisender. Hoerst du mich?" } },
        },
    })
    -- 3. one tone per language, made by Lua
    risen.dialog.create({
        name = "EX_Voice_Synth", owner = NPC, permanent = true,
        lines = {
            { speaker = "npc", listener = "player", text = { default = "This voice is a generated tone.", de = "Diese Stimme ist ein erzeugter Ton." },
              sound = { en = { data = tone_wav(523, 0.4) }, de = { data = tone_wav(392, 0.4) } } },
        },
    })
    -- 4. sound only (no subtitle), from an mp3
    risen.dialog.create({
        name = "EX_Voice_Cue", owner = NPC, permanent = true,
        lines = { { command = "SaySound", Speaker = "npc", sound = "sounds/cue.mp3" } },
    })
    -- 2. one ogg file for every language; the last line ends the conversation
    risen.dialog.create({
        name = "EX_Voice_Bye", owner = NPC, permanent = true,
        lines = {
            { speaker = "npc", listener = "player", text = { default = "Farewell!", de = "Lebe wohl!" }, sound = "sounds/farewell.ogg" },
            { command = "End" },
        },
    })
end

-- ---- the NPC: declared once (`define` runs on load.before and every session.start) -----------------------------------------------------
risen.npc.persistent({
    name = NPC, template = "Peasant_1_01", offset = { 150, 20, 150 }, label = "Voice Tester", peaceful = true, define = define_content,
})

-- the live check belongs to the test run only
if risen.test.is_active() then
    require("test_voice")(NPC)
end
