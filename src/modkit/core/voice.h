// Every text of a mod and of the game in one table, and the voice recording workflow on top of it (docs/editor/REQUIREMENTS.md VOX-1..8,
// docs/editor/USER_GUIDE.md "Voice recording"): the rows the Strings panel shows and filters, the export a studio records from (CSV, JSON,
// one script per speaker), the matcher that assigns a folder of recordings to the lines, and the change of the project that imports them.
//
// Line ids. A dialogue node of the project has the id `<mod id>_<first 8 hex of the node uid>` (an option node: the same as its default info name),
// so the id survives saving, reloading, renaming the dialogue and moving the node; the expected recording of a language is `<id>_<Language>.ogg`.
// The layer plays it under the name `INFO_SPASS_<mod>_<info>_<n>_<Language>_%_SPASS.wav` (`runtime_sound`, src/common/sound_file.h), where `n` is the
// line's place in its info. Other texts of the project have ids of the form `<kind>.<name>.<field>` (`item.MK_Sword.label`) or their string id (free
// string rows); game rows have the id of their string table row (`INFO_FILLERX2_00002231`).
#pragma once

#include <functional>
#include <map>
#include <string>
#include <vector>

#include "core/project.h"

namespace mk {

class GameData;

struct StringLine {
    std::string id;
    bool game = false;   // a row of the game's string tables: read-only, "override in the mod" makes a free string row of it
    std::string kind;    // mod: dialogue | quest | event | item | recipe | spell | npc_template | npc | layer | string; game: the table ("infos", "items")
    std::string field;   // mod: option | line | command | log | text | label | description | title; game: ""
    std::string object;  // the dialogue / quest / item ... it belongs to; game: the info (infos.tab "Info") or ""
    std::string object_uid, node_uid;  // mod: the object and, for graph nodes, the node
    std::string string_id;  // the engine's text id the row writes / is (free string rows, FO_<name> of labels, game rows); "" when it has none
    bool spoken = false;    // a line someone says: it can have a recording
    std::string speaker, voice, listener;  // speaker: the entity ("PC_Hero", "Tucker"); voice: the NPC voice ("Player", "Fincher")
    std::string quest, parent;  // parent: the line id of the option this line answers / follows, game: the parent info
    std::string context;        // a readable place: "Dialogue Greeting > option 'Who are you?'"
    int order = 0;              // dialogue lines: the place in the info (1 = the option's own line), 0 when none
    Text text, stage;           // per language; stage: stage directions for the speaker (game: <Language>_StageDir, mod: node data "stage")
    std::map<std::string, std::string> audio;  // "default" / language -> mod: the sound reference of the node; game: the raw WaveFile name
    std::map<std::string, bool> shipped;       // game: language -> the game has the compiled speech file
    std::string runtime_key;                   // mod dialogue lines: "<info>_<n>", the risen.voice key of the line
    bool overridden = false;                   // a game row that a row of the project replaces (same string id)
    std::string search;                        // lower-case haystack of the filter: id, texts, speaker, voice, object, quest
};

// the id of a dialogue node's line
std::string voice_line_id(const Project& p, const GraphNode& n);
// every text of the project (dialogue nodes in the order of their infos, quests, events, items, recipes, spells, NPCs, layers, free string rows).
// `game` (optional) fills the NPC voices of the speakers.
std::vector<StringLine> project_lines(const Project& p, const GameData* game = nullptr);
// every row of the game's string tables; spoken rows (a WaveFile column) know their speaker, voice, info, quest and which shipped speech files exist
std::vector<StringLine> game_lines(const GameData& g);
// marks the game rows whose string id a project row writes (case-insensitive)
void mark_overridden(std::vector<StringLine>& game, const std::vector<StringLine>& mod);

enum class AudioStatus { None, Recorded, Missing };  // None: not a spoken line
// mod: the line names a sound for the language (or a "default" one) that is an asset of the project or a game sound; game: the shipped file exists
AudioStatus audio_status(const Project& p, const StringLine& l, const std::string& language);
const char* audio_status_name(AudioStatus s);  // "", "recorded", "missing"
// the file a studio should deliver for a language: mod "<id>_<Language>.ogg", game the WaveFile name of the language ("" when not spoken)
std::string expected_audio_file(const StringLine& l, const std::string& language);
// the name the layer / the engine plays the line under: mod "INFO_SPASS_<mod>_<key>_<Language>_%_SPASS.wav", game the WaveFile name
std::string runtime_sound(const Project& p, const StringLine& l, const std::string& language);
// the text of a language with the project's fallback (the first language, then any)
std::string text_of(const Text& t, const std::string& language, const std::vector<std::string>& fallback = {});

// ---- editing a project row in place
// sets the text / stage direction of a language on the object the row comes from; false for game rows and rows without that field
bool set_line_text(Project& p, const StringLine& l, const std::string& language, const std::string& text);
bool set_line_stage(Project& p, const StringLine& l, const std::string& language, const std::string& text);
// a free string row with the game row's id and texts (the "override in the mod" of a game row); returns its uid ("" when one exists already)
std::string override_game_line(Project& p, const StringLine& game_row, const std::vector<std::string>& languages);

// ---- export for the studio
struct VoiceExportOptions {
    std::vector<std::string> languages;  // empty: the project's languages
    bool missing_only = false;           // only spoken lines without a recording in the language
    bool spoken_only = true;             // false: every text (item names, quest texts, HUD messages ...), with an empty status
    bool scripts = true;                 // one plain text script per speaker (scripts/<speaker>.txt)
};
struct VoiceRow {
    const StringLine* line = nullptr;
    std::string language, status, expected_file, runtime_sound, current_file;
};
std::vector<VoiceRow> voice_rows(const Project& p, const std::vector<const StringLine*>& lines, const VoiceExportOptions& o);
// the CSV columns, in order (docs/editor/USER_GUIDE.md "Voice recording")
const std::vector<std::string>& voice_csv_columns();
std::string voice_csv(const std::vector<VoiceRow>& rows);  // UTF-8 with BOM, CRLF, RFC 4180 quoting
json voice_json(const Project& p, const std::vector<VoiceRow>& rows);
// speaker -> the plain text of his script (lines in order, with id, context, stage direction and expected file)
std::map<std::string, std::string> voice_scripts(const Project& p, const std::vector<VoiceRow>& rows);
struct VoiceExportResult {
    bool ok = false;
    std::string error;
    std::vector<std::string> written;  // file paths
    size_t rows = 0;
};
VoiceExportResult write_voice_export(const Project& p, const std::vector<const StringLine*>& lines, const VoiceExportOptions& o, const std::string& out_dir);

// ---- import of the recordings
// a parsed CSV of the export (or any CSV with the columns line_id, language and file): the rows that name a file
struct CsvAssignment {
    std::string line_id, language, file;
};
std::vector<std::vector<std::string>> parse_csv(const std::string& text);  // RFC 4180, a BOM and CRLF / LF accepted
std::vector<CsvAssignment> csv_assignments(const std::string& text, std::string& error);

struct VoiceMatch {
    std::string file;      // the recording (path as given)
    std::string line_id;   // the line it is for
    std::string node_uid;  // the dialogue node
    std::string language;  // the language it is for
    std::string how;       // "name", "name (default language)", "runtime name", "csv"
};
struct VoiceMatchReport {
    std::vector<VoiceMatch> matched;
    std::vector<std::pair<std::string, std::string>> unmatched;                  // file, why
    std::vector<std::pair<std::string, std::vector<std::string>>> ambiguous;    // file, the "line_id/Language" it could be for
    std::vector<std::pair<std::string, std::vector<std::string>>> duplicates;   // "line_id/Language", the files that are all for it (none is taken)
};
// files -> lines. `lines` are the candidates (spoken project rows; others are ignored), `languages` the languages a file name may end with,
// `default_language` the language of a file named after the line id alone. With `csv`, a file the CSV names is assigned by it (its bare name or the
// path relative to the folder: both case-insensitive); other files are matched by name.
VoiceMatchReport match_recordings(const Project& p, const std::vector<StringLine>& lines, const std::vector<std::string>& files,
                                  const std::vector<std::string>& languages, const std::string& default_language,
                                  const std::vector<CsvAssignment>* csv = nullptr);
bool is_audio_candidate(const std::string& file);  // an extension a recording may have (ogg, wav, mp3, flac, opus, m4a, aac, aif(f), wma, webm)

// the asset file name a recording gets: "<id>_<Language>.<ext>" (ext "ogg" after a conversion, otherwise the file's own)
std::string voice_asset_name(const std::string& line_id, const std::string& language, const std::string& ext);
// one recording that is in the project's folder now (rel_path relative to the project file, e.g. "voice/x_English.ogg")
struct ImportedVoice {
    std::string node_uid, language, rel_path;
};
// assigns the recordings to their nodes ("sound" of the node, the language's entry) and adds the files to the assets (one undo step for the caller);
// returns how many nodes changed
int apply_voice_import(Project& p, const std::vector<ImportedVoice>& files);

}  // namespace mk
