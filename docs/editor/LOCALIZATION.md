# Localizing risen-modkit

The editor's own texts (menus, labels, help texts, problem messages, node titles) can be translated. This is not the
**mod's** texts: a mod project keeps its texts per language in the project (Strings panel); the UI language of the
editor and the languages of a mod are independent.

* **English is embedded.** The English text is the lookup key and the fallback, so the editor works with no language
  file at all, and a text a language file does not translate stays English.
* **Other languages are TOML files**, one per language. **German (`de`) ships with the editor.**
* The language is chosen in **Window > Settings > Interface > Language** (*Automatic* follows the operating system, so a
  German Windows starts in German). The switch applies at once; the choice is stored in `settings.json`
  (`"language": "auto" | "en" | "de" | ...`). `risen-modkit --lang de` overrides it for one run. Screenshot runs (`--screenshot`, used by tests and docs) are English unless `--lang` is given, so the system language never changes what they show.

## Where the files are

`risen-modkit` looks for `*.toml` files in

1. `lang/` next to the executable (what the release zip and the build output contain; the source is `src/modkit/lang/`),
2. the per-user configuration folder, `lang/` below it (`%APPDATA%\risen-modkit\lang`, `~/.config/risen-modkit/lang`,
   `~/Library/Application Support/risen-modkit/lang`).

A file in the user folder **replaces** a shipped file with the same `code`, so a community translation can be dropped in without
touching the installation. *Reload language files* in the Settings re-reads the files of the active language, which is
handy while translating.

## The file format

```toml
[meta]
code = "de"                  # the language code; "pt-BR" is found for "pt-br" and, failing that, "pt"
name = "Deutsch"             # the language's own name, shown in the list
english_name = "German"
author = "..."

[strings]
# the key is the English text exactly as it appears in the source
"Open project..." = "Projekt öffnen..."
"Saved {0}" = "{0} gespeichert"
"%zu errors, %zu warnings%s" = "%zu Fehler, %zu Warnungen%s"
"menu|Open" = "Öffnen"       # with a context: see below
"Not translated yet" = ""    # an empty value counts as "not translated": the English text is shown
```

Rules:

* **Keep the placeholders.** Two kinds exist and a translation must contain the same ones as the English text:
  * `{0}`, `{1}`, ... (texts the editor fills in with `trf`): the translation may **reorder** them.
  * printf specifiers (`%s`, `%d`, `%zu`, `%.1f`, ...): they are filled in **in the order of the arguments**, so keep their order.
  `python tools/i18n.py check` and the unit test `i18n_shipped_language_files` compare them.
* `\n` is a line break, `\"` a quote, `\\` a backslash (TOML basic strings); a literal `{` in a text that goes through `trf` is written `{{`.
* **Context.** Where one English word has two meanings, the source says `trc("menu", "Open")` and the key is `"menu|Open"`; without such an
  entry the plain `"Open"` is used.
* **ImGui ids.** A label like `Save##project` is looked up as `Save` and keeps its `##project` id part; a translation never needs to carry it.
  Two different English texts that translate to the same word and meet in the same window (two buttons both called "Einfügen") would
  share an ImGui id and only one of them would react; if that happens, differentiate the translations.
* The font is a system sans-serif (Segoe UI, Arial, DejaVu Sans, Noto Sans), loaded with the glyphs it has: Latin, Cyrillic and
  Greek work, Chinese / Japanese / Korean would need a font that has them (not wired up yet).

## Translating: the tool

```
python tools/i18n.py extract            rewrite src/modkit/lang/template.toml: every text of the source, empty translations
python tools/i18n.py update xx          create / refresh src/modkit/lang/xx.toml: new texts are added empty, translations stay,
                                        texts the source no longer has move to [obsolete]
python tools/i18n.py check [xx] [-v]    translated / unknown / broken counts, exit code 1 for a broken placeholder (--strict: 2 for gaps)
```

A new language: `python tools/i18n.py update xx`, fill `[meta]` and the values, `check xx`, copy the file to the `lang` folder of the
editor (or to the user folder) and pick it in the Settings. The tool reads the source for `tr("...")`, `trc("ctx", "...")`,
`trf("...", {...})` and `N_("...")` (adjacent string literals are joined); the CI runs `check` so a broken placeholder fails the build.
The shipped `de.toml` is complete (1500 texts); `template.toml` is generated, do not edit it.

## For developers: how texts get into the source

`core/i18n.h` (in `modkit_core`, so the validation messages of the core use it too):

| Call | Use |
|---|---|
| `tr("English")` | a literal text; returns the translation or the very same pointer. `ImGui::Button(tr("Save"))`, `ImGui::TextWrapped("%s", tr("..."))` (a text without arguments goes through `"%s"`, never as the format), `ImGui::Text(tr("Count: %d"), n)` for a printf text with arguments |
| `trf("... {0} ... {1}", {a, b})` | a text with values; replaces the placeholders in the translation (so a language can reorder), the values are `std::string` / `const char*` / `std::string_view` |
| `trc("context", "English")` | disambiguation |
| `N_("English")` | marks a literal in a **static table** (`static const char* kModes[] = {N_("a"), N_("b")}`, the node tables of `core/graph.cpp` and `core/events.cpp`, `TemplateAction` labels, the `kPanel*` titles): it is extracted but not translated where it stands, the place that **shows** it calls `tr()` (`ui::combo_items` for an `ImGui::Combo` over such a table) |

Rules of thumb:

* **Never translate what is data**: ids, template names, keys that are compared or stored (`"Running"`, `"enters"`, a skill name, a
  JSON field), text of the mod project, log lines (`log_*`), the Lua that is exported. A status word that is shown *and* compared is shown
  through `tr()` and compared in English (see the quest state combo).
* **Panel titles** are window ids: `ui::panel_label(title)` gives `tr(title) + "###" + title` (translated text, English id, so the docking
  layout survives a language switch), `ui::panel_id(title)` the id alone for `DockBuilderDockWindow` / focus.
* A text is **one whole sentence**: do not build a sentence from translated pieces (`tr("Placed ") + name`); use `trf("Placed {0}", {name})` or
  two whole texts, otherwise the word order of other languages cannot be expressed.
* `kind_name(TplKind)` is the English name for logic and messages, `kind_label(TplKind)` the translated one for the UI.
* The language is switched between frames (`App::set_language` defers it), because pointers returned by `tr()` are valid until the language changes.
* Adding a text: write it with `tr` / `trf` / `N_`, run `python tools/i18n.py update de` (adds it empty), translate it, `check`.
  The unit tests (`i18n_*` in `tests/modkit/i18n_tests.inc`) cover the lookup rules, discovery and the shipped files.
