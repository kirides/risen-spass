-- Live check of example_voice (only loaded in a test run). `-Param lang=German` together with `-VoiceLanguage German` checks the German variants.
local T = risen.test

return function(NPC)
    local lang = T.param("lang", "English")
    T.plan { "voice language", "voice files registered", "variants compiled", "convention voices the line", "variant of the voice language",
             "voiced line takes the audio time", "unvoiced line takes the fallback time", "speaking while the audio plays", "sound is playable",
             "explicit ogg file", "ogg is Vorbis for FMOD", "tone made by Lua", "SaySound line from an mp3", "mp3 is MPEG for FMOD", "bark",
             "bark ends with the audio", "bad file refused", "bad data refused", "bad audio refused before anything is created",
             "language fallback", "loose sound shadows a shipped speech file" }

    -- plays one option of the open conversation; returns the frames it took, the frames the NPC was speaking, and the last custom voice key the engine asked for
    local function play(npc, name)
        local info = assert(risen.dialog.get(name), name)
        local ticks, speaking = 0, 0
        local h = risen.on("tick", function()
            ticks = ticks + 1
            if risen.voice.speaking(npc) then speaking = speaking + 1 end
        end)
        risen.voice.last()  -- (the engine's last request stays until the next voiced line)
        info:run()
        T.wait_event("info.finished", 60, function(n) return n == name end)
        risen.off(h)
        T.sleep(30)
        return ticks, speaking, risen.voice.last()
    end

    T.run(function()
        T.wait_event("session.start", 180, function(m) return m == "new" end)
        T.wait_until(function() return risen.entity.get(NPC) ~= nil end, 30, "the NPC was spawned")
        T.sleep(60)
        local player = assert(risen.entity.player())
        local npc = assert(risen.entity.get(NPC))

        T.section("registration")
        T.eq("voice language", risen.voice.language(), lang)
        local greet = risen.voice.get("EX_Voice_Greet_2")
        T.expect("voice files registered", greet ~= nil and greet.variants.English ~= nil and greet.variants.German ~= nil, "EX_Voice_Greet_2 from voice/<language>/")
        local ok = greet ~= nil
        for _, v in pairs(greet and greet.variants or {}) do ok = ok and v.duration_ms >= 290 and v.duration_ms <= 310 end
        T.expect("variants compiled", ok, "0.3 s tones")

        -- a control: the same kind of line without any audio shows how long a line takes by default
        risen.dialog.create({ name = "EX_Voice_Mute", owner = NPC, permanent = true,
            lines = { { speaker = "npc", listener = "player", text = "This line has no voice at all." } } })

        risen.dialog.start(player, npc)
        T.sleep(120)

        T.section("a line voiced by the directory convention")
        local ticks, speaking, last = play(npc, "EX_Voice_Greet")
        T.expect("convention voices the line", last ~= nil and last.key == "EX_Voice_Greet_2", last and last.key)
        T.eq("variant of the voice language", last and last.language, lang)
        local snd = last and risen.assets.sound(last.title)
        T.expect("sound is playable", snd ~= nil and snd.state == 3 and snd.fmod == true and math.abs((snd.fmod_length_ms or 0) - (snd.duration_ms or -1)) <= 25,
            snd and ("state %s fmod %s length %s/%s ms"):format(tostring(snd.state), tostring(snd.fmod), tostring(snd.fmod_length_ms), tostring(snd.duration_ms)))

        T.section("timing")
        local bye_ticks, bye_speaking
        local mute_ticks, mute_speaking = play(npc, "EX_Voice_Mute")
        local synth_ticks, synth_speaking, synth_last = play(npc, "EX_Voice_Synth")
        T.expect("unvoiced line takes the fallback time", mute_ticks > 250 and mute_speaking == 0, ("%d frames, speaking in %d"):format(mute_ticks, mute_speaking))
        T.expect("voiced line takes the audio time", synth_ticks < mute_ticks - 120 and synth_ticks < 200, ("voiced %d frames vs unvoiced %d"):format(synth_ticks, mute_ticks))
        T.expect("speaking while the audio plays", synth_speaking >= 20 and synth_speaking <= 60, ("%d frames (0.4 s is about 32 at 80 frames/s)"):format(synth_speaking))

        T.section("other sources")
        T.expect("tone made by Lua", synth_last ~= nil and synth_last.key == "EX_Voice_Synth_1" and synth_last.language == lang, synth_last and (synth_last.key .. "/" .. synth_last.language))
        local cue_ticks, cue_speaking = play(npc, "EX_Voice_Cue")
        local cue = risen.voice.get("EX_Voice_Cue_1")
        local cue_snd = cue and risen.assets.sound(cue.variants.default.title)
        T.expect("SaySound line from an mp3", cue_speaking >= 10 and cue_ticks < 200, ("%d frames, speaking in %d"):format(cue_ticks, cue_speaking))
        T.expect("mp3 is MPEG for FMOD", cue_snd ~= nil and cue_snd.fmod == true and cue_snd.type == 9, cue_snd and ("type %s"):format(tostring(cue_snd.type)))

        T.section("bark")
        local before = risen.voice.speaking(npc)
        local barked = risen.voice.say(npc, "EX_Voice_Synth_1")
        T.sleep(8)
        T.expect("bark", barked == true and before == false and risen.voice.speaking(npc) == true, ("say=%s before=%s now=%s"):format(tostring(barked), tostring(before), tostring(risen.voice.speaking(npc))))
        T.sleep(120)
        T.expect("bark ends with the audio", risen.voice.speaking(npc) == false)

        -- the last option says goodbye (explicit ogg file) and ends the conversation
        local bye_t, bye_s, bye_last = play(npc, "EX_Voice_Bye")
        local bye = risen.voice.get("EX_Voice_Bye_1")
        local bye_snd = bye and risen.assets.sound(bye.variants.default.title)
        T.expect("explicit ogg file", bye_last ~= nil and bye_last.key == "EX_Voice_Bye_1" and bye_s >= 10, ("%s speaking %d"):format(tostring(bye_last and bye_last.key), bye_s))
        T.expect("ogg is Vorbis for FMOD", bye_snd ~= nil and bye_snd.fmod == true and bye_snd.type == 10, bye_snd and ("type %s"):format(tostring(bye_snd.type)))

        T.section("refusals")
        local bad, why = risen.dialog.create({ name = "EX_Voice_Bad", owner = NPC, lines = { { speaker = "npc", text = "x", sound = "voice/nothing.ogg" } } })
        T.expect("bad file refused", bad == nil and tostring(why):find("cannot read", 1, true) ~= nil and risen.dialog.get("EX_Voice_Bad") == nil, tostring(why))
        local info, why2 = risen.assets.import_sound("Spass_Bad.wav", { data = string.rep("not audio ", 20) })
        T.expect("bad data refused", info == nil and tostring(why2):find("unsupported audio format", 1, true) ~= nil, tostring(why2))
        local bad3, why3 = risen.dialog.create({ name = "EX_Voice_Bad2", owner = NPC, lines = { { speaker = "npc", text = "x", sound = { data = "RIFFxxxxWAVE" } } } })
        T.expect("bad audio refused before anything is created", bad3 == nil and risen.dialog.get("EX_Voice_Bad2") == nil and risen.voice.get("EX_Voice_Bad2_1") == nil, tostring(why3))

        T.section("language fallback and shadowing")
        -- no entry for the voice language and no default: English if present, else the first one by name
        local f, why4 = risen.voice.set("EX_Voice_Fallback", { French = "voice/German/EX_Voice_Greet_2.wav", Polish = "voice/English/EX_Voice_Greet_2.wav" })
        local title = risen.voice.title("EX_Voice_Fallback")
        T.expect("language fallback", f ~= nil and title ~= nil and title:find("_French_", 1, true) ~= nil, tostring(title) .. " " .. tostring(why4))

        -- a loose file named like a shipped speech sound is found instead of it (the resource must not be loaded yet: take a line of a character
        -- the game has not talked to; the hero's own lines are prefetched at the start)
        local dir = "data/compiled/speech_" .. lang:lower() .. "/infos/Chapter3"
        local names = risen.vfs.find(dir, "*._xsnd") or {}
        local shipped = (names[1] or "none._xsnd"):gsub("%._xsnd$", ".wav")
        local before_info = risen.assets.info(shipped)
        local imported, why5 = risen.assets.import_sound(shipped, "voice/English/EX_Voice_Greet_2.wav")
        local after_info = risen.assets.info(shipped)
        risen.voice.say(npc, shipped)
        T.sleep(10)
        local sh = risen.assets.sound(shipped)
        local res = risen.assets.resource(shipped)
        risen.assets.remove("voice/" .. (imported and imported.compiled or "x"))
        risen.assets.refresh()
        T.expect("loose sound shadows a shipped speech file", before_info.source == "game" and after_info.loose == true and sh ~= nil and sh.state == 3 and sh.fmod == true
            and math.abs((sh.fmod_length_ms or 0) - 300) <= 5 and res ~= nil and res.path:find("generated", 1, true) ~= nil,
            ("%s: before %s, after %s (loose %s), FMOD plays %s ms from %s (the shipped line is longer; its Duration property stays) %s"):format(shipped, tostring(before_info.source),
                tostring(after_info.source), tostring(after_info.loose), tostring(sh and sh.fmod_length_ms), tostring(res and res.path), tostring(why5)))
    end, "ex_voice")
end
