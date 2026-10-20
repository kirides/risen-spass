---@diagnostic disable: need-check-nil
-- Checks the voice recording workflow of risen-modkit in a running game: examples/modkit/voice.rmk.json got its recordings with
--   risen-modkit voice.rmk.json --voice-import voice_recordings --csv voice_recordings/voice_lines.csv
-- (a stereo 48 kHz FLAC converted by ffmpeg to mono 44.1 kHz Ogg Vorbis, a WAV copied as it is, an AIFF named freely in the filled CSV and converted)
-- and was exported to mods/modkit_voice. The NPC's line and the hero's option line must play those files.
--   .\scripts\test-run.ps1 -Instance 6 -Mods modkit_voice,modkit_voice_driver -WaitForResult -Seconds 240 -Scenario modkit_voice [-VoiceLanguage German -Param lang=German]
local T = risen.test
local NPC = "MKV_Speaker"

T.plan {
    "the imported recordings are registered",
    "the converted file has the length of the recording",
    "the NPC's line asks for the imported recording",
    "the converted Ogg Vorbis plays in FMOD",
    "the NPC speaks while the recording plays",
    "the option line plays the recording the CSV named",
    "a line without a recording has no voice entry",
}

-- plays one option of the open conversation: the frames it took, the frames the NPC was speaking, the voice keys the engine asked for
local function play(npc, name)
    local info = assert(risen.dialog.get(name), name)
    local ticks, keys = 0, {}
    local first, last_t  -- the clock of the first and the last frame the NPC was speaking (seconds; frame counts vary with the load of the machine)
    local h = risen.on("tick", function()
        ticks = ticks + 1
        if risen.voice.speaking(npc) then
            last_t = os.clock()
            first = first or last_t
        end
        local last = risen.voice.last()
        if last then keys[last.key] = last end
    end)
    info:run()
    T.wait_event("info.finished", 60, function(n) return n == name end)
    risen.off(h)
    T.sleep(30)
    return ticks, first and (last_t - first) or 0, keys
end

T.run(function()
    local lang = T.param("lang", "English")
    T.wait_event("session.start", 180, function(m) return m == "new" end)
    T.wait_until(function() return risen.entity.get(NPC) ~= nil end, 60, "the NPC was spawned")
    T.sleep(60)
    local player = assert(risen.entity.player())
    local npc = assert(risen.entity.get(NPC))

    T.section("registration")
    local line = risen.voice.get("MKV_Hello_2")
    local en = line and line.variants.English
    local de = line and line.variants.German
    T.expect("the imported recordings are registered", en ~= nil and de ~= nil and risen.voice.get("MKV_Hello_1") ~= nil,
        line and ("variants: %s %s"):format(tostring(en and en.file), tostring(de and de.file)) or "no entry MKV_Hello_2")
    T.expect("the converted file has the length of the recording", en ~= nil and math.abs(en.duration_ms - 1200) <= 30 and de ~= nil and math.abs(de.duration_ms - 900) <= 30,
        ("English %s ms (1200), German %s ms (900)"):format(tostring(en and en.duration_ms), tostring(de and de.duration_ms)))

    T.section("the conversation")
    risen.dialog.start(player, npc)
    T.sleep(120)
    local ticks, speaking, keys = play(npc, "MKV_Hello")
    local asked = keys.MKV_Hello_2
    T.expect("the NPC's line asks for the imported recording", asked ~= nil and asked.language == lang, asked and (asked.key .. "/" .. asked.language .. " " .. asked.title) or "not asked")
    local snd = asked and risen.assets.sound(asked.title)
    T.expect("the converted Ogg Vorbis plays in FMOD", snd ~= nil and snd.fmod == true and snd.state == 3 and (lang ~= "English" or snd.type == 10)
        and math.abs((snd.fmod_length_ms or 0) - (lang == "English" and 1200 or 900)) <= 30,
        snd and ("state %s fmod %s type %s length %s ms channels %s"):format(tostring(snd.state), tostring(snd.fmod), tostring(snd.type), tostring(snd.fmod_length_ms), tostring(snd.channels)) or "no sound")
    -- the NPC's output lasts as long as the recording (a line without audio is not "speaking" at all)
    local want = lang == "English" and 1.2 or 0.9
    T.expect("the NPC speaks while the recording plays", speaking >= want * 0.6 and speaking <= want + 0.8,
        ("%.2f s speaking (the recording: %.1f s), %d frames for the option"):format(speaking, want, ticks))
    local opt = keys.MKV_Hello_1
    local optv = risen.voice.get("MKV_Hello_1")
    T.expect("the option line plays the recording the CSV named", opt ~= nil and optv ~= nil and optv.variants.English ~= nil and math.abs(optv.variants.English.duration_ms - 700) <= 30,
        opt and (opt.key .. " " .. tostring(optv and optv.variants.English and optv.variants.English.duration_ms)) or "the option line was not asked for")
    T.expect("a line without a recording has no voice entry", risen.voice.get("MKV_Bye_2") == nil)
    T.finish("modkit_voice done")
end, "modkit_voice")
