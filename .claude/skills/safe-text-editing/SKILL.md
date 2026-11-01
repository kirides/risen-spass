---
name: safe-text-editing
description: How to write and edit text and scripts on this machine without the shell eating newlines, backslashes and quotes (Windows, Git Bash and PowerShell, CRLF working tree). Use before any heredoc, sed, python -c, echo/cat >>, multi-line commit message, or edit of a file that contains backslashes, quotes or regexes.
---

# Writing text without the shell damaging it (short form)

Things that went wrong here: a backslash-n inside a Bash heredoc became a real newline, `\\.\pipe\...` lost half its backslashes, a Python
heredoc with nested quotes died with `unexpected EOF while looking for matching '`, a `-Mods a,b` array arrived as one string.

## Rules
1. **Use the file tools, not the shell, to write text.** `Write` for new files, `Edit` for changes (exact string, unique match; add context to make it unique).
   They pass bytes through untouched. This is the default for source, docs, TOML/JSON/Lua and for **scripts**.
2. **Anything non-trivial that must run as code: write a script file, then run it.** Not `python - <<EOF`, not `python -c "..."`.
   Pattern for a multi-edit patch: `Write` `scratch_patch.py` (use `assert s.count(old) == 1` before each `replace`), run `python scratch_patch.py`, **delete it** (`rm`), check with `git diff --stat`.
3. **Heredocs only for plain text with no backslashes or quotes that matter**, and quote the delimiter (`<<'EOF'`). A commit message heredoc in Bash is fine (`git commit -F - <<'EOF'`); in PowerShell use a single-quoted here-string `@' ... '@` with the closing `'@` at column 0.
4. **Backslashes:** in Python build them with `chr(92)` or raw strings in a *file*; in Bash use forward slashes (`B:/DevDrive/...` works for most tools); in PowerShell single quotes keep them literal (`'B:\DevDrive\x'`). After any shell-made edit of a path or regex, `grep -n` the result.
5. **Line endings:** the working tree is CRLF (`autocrlf`). Python in text mode converts; for patches read/write with `newline=''`, detect `'\r\n' in s`, work on LF (`s.replace('\r\n','\n')`) and convert back. Never leave a mixed file. `git`'s "LF will be replaced by CRLF" warning is harmless.
6. **Verify after writing:** `grep -n` the key line, `git diff --stat` (is the diff the size you expected? a huge diff means line endings or an encoding changed), build/test. Do not re-`Read` a file you just `Edit`ed (the tool errors if it failed).
7. **Encoding:** UTF-8 without BOM. Engine texts are Windows-1252; German umlauts in `de.toml` go through `Edit` as plain UTF-8.
   **In a Python patch script always `open(path, encoding='utf-8', newline='')`** (read and write): Python's default here is cp1252, so new umlauts land as single bytes and the file stops being valid UTF-8 (it broke `de.toml`: `UnicodeDecodeError ... 0xe4`). After touching a text file with non-ASCII, check `open(p,'rb').read().decode('utf-8')`.
8. **Edit tool needs a prior `Read`** of the file in this conversation; if the file changed on disk since, take the current state (do not undo).
9. **Pass arguments, not interpolated text:** give scripts paths and JSON via argv (`python -I tool.py <path> '<json>'`), single-quoted in Bash; for JSON with quotes prefer a file (`@args.json`) or build it in the script.
10. **Regex/`sed` on source:** avoid `sed -i` for anything with slashes or quotes; use `Edit` or a patch script. `rg`/`Grep` tool for searching (Grep escapes: literal braces need `\{`).

## Quick checks when output looks wrong
- Unexpected EOF / odd quotes -> the shell parsed your text: move it into a file.
- Paths with half the backslashes -> a shell layer unescaped them: use forward slashes or a file.
- Everything on one line / extra blank lines -> CRLF vs LF mismatch: see rule 5.
