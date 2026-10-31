# PakView – browse and edit Risen archives (`*.pak`, `*.p00` ...)

A WinRAR / 7-Zip like desktop tool in C# (.NET 10, Avalonia 12.1.4, DataGrid 12.1.2, compiled bindings) in `src/pakview/`. Format: RE.md section 43 (`G3V0`).

```powershell
cd src\pakview
dotnet run --project PakView -- "B:\SteamLibrary\steamapps\common\Risen\data\common\strings.pak"
dotnet test Risen.Pak.Tests          # 22 tests, no game needed (one re-saves a real archive when the game is installed)
```

| Project | Role |
|---|---|
| `Risen.Pak` | The library: reader (`PakArchive.Open`), edit operations, saver, format decoders (`Formats/`). No UI dependency. |
| `PakView` | The Avalonia app: tree + file list + live preview, drag and drop, dialogs. |
| `Risen.Pak.Tests` | xunit: round trips, atomic save, failure handling, name/path safety, decoders. |

## Code layout (MVVM with CommunityToolkit.Mvvm)

`src/pakview/PakView/`, plain MVVM: the view models hold the behaviour and know no control; the views only bind.

| Folder | What is in it |
|---|---|
| `Models/` | `EntryRow` (a list row), `DirItem` (tree node), `Describe` (sizes, icons, kinds). |
| `ViewModels/` | `MainViewModel` (archive, tree, list, selection, history; the commands are in `MainViewModel.Commands.cs`), `PreviewViewModel` + `PreviewBuilder` (what to show for a file, decoded off the UI thread), the preview contents (`MessageContent`, `TextContent`, `ImageContent`, `TableContent`, `PropsContent`, `SoundContent`), `SortableList<T>`. Properties are `[ObservableProperty]` partial properties, commands `[RelayCommand]`. |
| `Views/` | `MainWindow`, `PreviewView` (a `DataTemplate` per content type), `SoundContentView`, `PropsContentView`, `TableContentView`, `WaveformControl`, small grid helpers (`GridSorting`, `GridCopy`). Compiled bindings (`x:DataType`) everywhere. |
| `Services/` | `IShell` (the one interface the view models need from the window: message boxes, file pickers, clipboard, close) with `WindowShell`; `AudioPlayer` (NAudio); `ExternalEdits`. |
| `DevScript.cs`, `StartupOptions.cs` | the developer switches below, kept out of the real code. |

The window code-behind is only what a control must do: mirror the grid selection into the view model, run the grid's keys (Enter, Delete, F2, Insert, Backspace) and Alt+arrows / mouse side buttons *before* the DataGrid sees them (the DataGrid uses Enter to move the selection, so a `KeyBinding` on it never wins), drag and drop, the "save before closing?" question.

## Native AOT build (Windows x64)

```powershell
.\scripts\pakview-publish.ps1          # -> dist\pakview-win-x64\PakView.exe (24 MiB, 42 MiB with the Skia/HarfBuzz/ANGLE DLLs; no .NET runtime needed)
```
Needs the .NET 10 SDK and the Visual Studio C++ workload (native linker). The script adds the Visual Studio Installer folder to `PATH`: without it the link step fails with "vswhere.exe ... nicht gefunden". `PublishAot` is set in `PakView.csproj`, so any `dotnet publish -r win-x64` is AOT.

What had to change for AOT (nothing of it works by luck, each was seen failing):
* **No reflection bindings in code.** `new Binding("Name")` found nothing once the linker had trimmed the properties (blank property grid). The preview grids use template columns (`FuncDataTemplate`), the XAML uses compiled bindings (`x:DataType`).
* **Own column sorting** (`GridSort<T>`): the DataGrid decides whether a column can sort by reflecting over the item type; under AOT that silently gave "nothing sorts". Columns are marked sortable, the `Sorting` event is handled and the rows sorted by explicit comparisons (folders pinned first, header arrow). Ctrl+C copies selected grid rows as tab separated text.
* Remaining IL2104 / IL3053 warnings come from inside `Avalonia.Controls.DataGrid` (auto columns, path sorting), code paths PakView does not use; one summary line each.

**CI** (`.github/workflows/build.yml`): the job `pakview` runs the .NET unit tests and the AOT publish (the same script) on a Windows runner, checks that `PakView.exe` is a native build, and stages `PakView/`; the job `tools` merges that with the editor job's files into `risen-spass-tools-windows-x64.zip` (`PakView/PakView.exe` + its native libraries next to `risen-modkit.exe`, `risen-pack2mod.exe`, `lang/`), which the nightly release publishes.

Verified on the AOT exe: file grid, TAB0 grid, template / material property grids, DDS texture, sound playback (device state `Playing`), navigation script, and **real mouse clicks** on the header of the file grid (Size, arrow + order) and of the preview property grid (Value, arrow + order).

## What it does

* **Browse**: folder tree, sortable list (size, packed size, ratio, modified, type, state), search over the whole archive (`*`/`?` allowed, a `/` matches the full path).
* **Live preview** without extracting (a newer selection cancels the older job): text, `png/jpg/bmp/gif/webp`, the engine's `._ximg` textures (DDS DXT1/3/5, 32-bit, L8, A8; top mip, with alpha), `*.tab` string tables as a grid, entity templates `*.tple` and materials `._xmat` as a filterable property grid (property, value, set, type; header lines with the template's reference id or the images a material uses; ports of `tools/g3tple.py` / `g3mat.py`, `Risen.Pak/Formats/Properties.cs`), sounds (`._xsnd`, `.mp3`, PCM `.wav`): format facts, waveform, play / pause / stop, click-to-seek, volume, optional "play on select" (playback is Windows only, via NAudio WinMM; the waveform and facts work everywhere; MP3 decoding is NLayer), other `GENOMFLE` files (string table summary), other `GR01` resources (header + readable strings), always a hex tab and an info tab.
* **Explorer-like navigation**: back / forward history (toolbar buttons, `Alt+Left` / `Alt+Right`, mouse side buttons 4 / 5), `Alt+Up` and `Backspace` for the parent folder, double-click or `Enter` opens a folder; on a **sound** it plays it (the preview shows it), on other files it edits them with the external program.
* **Modify**: add files / folders (also by dropping them on the window), replace, rename, move (library), new folder, delete, "edit with external program" (extracted to a private temp folder, every save in the editor is staged into the archive; program/script types such as `.exe`, `.bat`, `.ps1` are never launched from an archive). New archive, Save As, discard (reload), verify (inflate every member and compare with the table of contents).
* **Extract as** (Edit menu / right click) offers only what the selected files really are, judged from their first bytes: a sound holding **MP3** offers just *Extract as MP3...* (the stored payload, byte for byte); a texture offers **DDS** (stored), **TGA** (decoded, 32 bit with alpha, top-left origin) and **PNG** (decoded RGBA) as a submenu. One available format collapses the menu into the single plain item; nothing applicable (a template, a text file ...) hides it; a mixed selection shows the union with "(sounds)" / "(images)" in the labels; folders are walked (structure kept) and files a format does not apply to are counted, not written. `Risen.Pak/Formats/AssetExport.cs` (`AvailableFormats`, `FormatsFor`; the library can still decode any sound to WAV: `ExportFormat.Wav`, used when a sound holds a WAV, which is then extracted as stored).
* **Extract** selection or everything; names are checked so nothing is written outside the chosen folder.

## How saving works (atomic)

Edits only change the in-memory table of contents. **Save** (`PakArchive.Save`):
1. writes the complete archive to a hidden temp file in the *same directory* (`.name.<guid>.tmp`): untouched members are copied byte for byte (stored size, deflate flag, FILETIMEs, attributes are kept; members that shared one body keep sharing it), new/replaced members are deflated when that saves at least ~3 % (menu switch), the table of contents is rebuilt and the header written last;
2. flushes it to disk and **verifies** it by re-reading it (same files, same sizes, new members inflate to their size);
3. closes the old file handle and switches with `File.Replace` over the old archive (NTFS `ReplaceFile`; `rename` elsewhere; optional `name.bak` of the previous archive) or `File.Move` for a new name;
4. reloads from the new file.

A failure or cancel in steps 1-3 removes the temp file and leaves the original untouched (tests: `FailedSaveLeavesOldArchiveAndNoTempFile`, `CancelledSaveLeavesNothingBehind`).

## Verified

* Unit tests (22) incl. re-saving the real `library.pak`: every member identical (SHA-1, size, compressed flag).
* Cross-check with the independent Python reader: a real `strings.pak` modified (add / replace / delete / rename) and saved by `Risen.Pak` is read correctly by `tools/g3pak.py`, untouched members are byte-identical.
* All 2679 templates of `templates.pak` and all 515 materials of `materials.pak` parse without error (test `RealTemplatesParse` / `RealMaterialsParse`); 6 multi-templates (`Objects/_MultiTemplates/*(n).tple`) have no properties the scanner recognises and fall back to the string table summary. Template values come from a tolerant scanner (as the Python tool): unfamiliar types show as hex.
* Sounds: the first 150 `._xsnd` of `sounds.pak` all decode (they are MP3 inside a `GR01SN04` container: payload offset/size at 0x10/0x14); a clip was pushed through a real `WaveOut` device with silence (state Playing, ended after its 0.57 s). **Not heard**: audible output was not listened to.
* UI rendered with the real game archives (`--shot`, below): `strings.pak` (TAB0 grid), `images.pak` (4096x2048 DXT3 `Layout_Gamepad._ximg`), `templates.pak` (search, template summary).
* **Not verified**: a game start from an archive written by the tool (the format follows `src/pack2mod/pak.cpp`, whose output the game loads, and keeps every field of the original); the UI edit commands were not driven by an automated UI test (the library behind them is); Linux/macOS (code is portable, not run).

Extract-as checks: all four pixel formats of the game's textures (DXT1/3/5, 32-bit, L8, A8; 16 textures) exported as DDS / TGA / PNG: Pillow opens every DDS (except the alpha-only A8, a Pillow limit), PNG and TGA are identical to each other and within 1 colour level of Pillow's own DXT decoding (rounding); the 10 `ui/` sounds as MP3 (frame sync `ff fb`, untouched payload) and WAV (Python's `wave` module reads all). Under the AOT build the UI command wrote PNG / TGA / DDS of the 4096x2048 `Layout_Gamepad` (and refused the "image as MP3" request with a message).

## Limits

No undo (use "Discard changes"); no audio playback (`._xsnd` shows header + hex); no TGA decoder; members of 4 GiB or more are refused (the format stores 32-bit sizes); members are copied as stored, never recompressed; patch chains (`.pak` + `.p00` ...) are opened one volume at a time.

## Developer switch

`PakView <archive> [--nav "dir:a/b;select:a|b;sort:Size;key:Enter;alt+up;x1;x2;probe:menu;dbl:name;extas:png:dir" --nav-log f.txt]` drives navigation with real routed key / mouse-button events and logs the folder after every step (verified: a 12-step history script gave the expected folders and back/forward counts; double-click on a sound gave `playing=True`).

`PakView <archive> [--dir path] [--filter text] [--select path] --shot out.png [--delay ms]` opens the archive, selects, renders the window to a PNG and exits (how the UI above was checked without a screen grab).
