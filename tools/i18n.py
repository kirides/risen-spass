#!/usr/bin/env python3
"""Translation tooling of risen-modkit (docs/editor/LOCALIZATION.md).

  python tools/i18n.py extract            write src/modkit/lang/template.toml (every text of the source, empty translations)
  python tools/i18n.py update [de ...]    merge the source texts into lang/<code>.toml: new texts get an empty entry,
                                          existing translations stay, texts that no longer exist move to [obsolete]
  python tools/i18n.py check [de ...]     report missing / obsolete / broken entries; exit code 1 when an entry is broken
                                          (placeholders or printf specifiers differ from the English text), 2 with --strict
                                          when texts are missing

The English text is the key. The source is scanned for tr("..."), trc("context", "..."), trf("...", {...}) and N_("...")
(adjacent literals are joined, C escapes decoded).
"""
import argparse
import re
import sys
import tomllib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / 'src' / 'modkit'
LANG = SRC / 'lang'
SCAN_DIRS = [SRC / 'app', SRC / 'core']

ESC = {'n': '\n', 't': '\t', 'r': '\r', '"': '"', "'": "'", '\\': '\\', '0': '\0', 'a': '\a', 'b': '\b', 'f': '\f', 'v': '\v'}


def decode(raw: str) -> str:
    out = []
    i = 0
    while i < len(raw):
        c = raw[i]
        if c != '\\':
            out.append(c)
            i += 1
            continue
        i += 1
        e = raw[i]
        if e == 'x':
            j = i + 1
            while j < len(raw) and raw[j] in '0123456789abcdefABCDEF':
                j += 1
            out.append(chr(int(raw[i + 1:j], 16)))
            i = j
        elif e == 'u':
            out.append(chr(int(raw[i + 1:i + 5], 16)))
            i += 5
        else:
            out.append(ESC.get(e, e))
            i += 1
    return ''.join(out)


LIT = r'"(?:[^"\\\n]|\\.)*"'
LITS = rf'(?:{LIT}\s*)+'  # adjacent literals (also across lines)
TRC = re.compile(rf'(?<![\w.>])trc\(\s*({LITS}),\s*({LITS})', re.S)
TRF_OR_TR = re.compile(rf'(?<![\w.>])(tr|trf|N_)\(\s*({LITS})', re.S)


def literal_value(s: str) -> str:
    return ''.join(decode(m[1:-1]) for m in re.findall(LIT, s))


def scan():
    """-> ordered dict key -> list of 'file:line' where key is 'text' or 'context|text'"""
    found = {}
    for d in SCAN_DIRS:
        for p in sorted(d.rglob('*')):
            if p.suffix not in ('.cpp', '.h', '.inc'):
                continue
            if p.name in ('i18n.h', 'i18n.cpp'):
                continue
            text = p.read_text(encoding='utf-8')
            rel = p.relative_to(ROOT).as_posix()
            hits = []
            for m in TRC.finditer(text):
                hits.append((m.start(), literal_value(m.group(1)) + '|' + literal_value(m.group(2))))
            for m in TRF_OR_TR.finditer(text):
                hits.append((m.start(), literal_value(m.group(2))))
            for pos, key in sorted(hits):
                line = text.count('\n', 0, pos) + 1
                head = key.split('##')[0] if '##' in key and not key.startswith('##') else key
                if not any(ch.isalpha() for ch in head.split('|')[-1]):
                    continue
                found.setdefault(head, []).append(f'{rel}:{line}')
    return found


def q(s: str) -> str:
    out = ['"']
    for ch in s:
        if ch == '"':
            out.append('\\"')
        elif ch == '\\':
            out.append('\\\\')
        elif ch == '\n':
            out.append('\\n')
        elif ch == '\t':
            out.append('\\t')
        elif ch == '\r':
            out.append('\\r')
        elif ord(ch) < 0x20 or ord(ch) == 0x7f:
            out.append('\\u%04x' % ord(ch))
        else:
            out.append(ch)
    out.append('"')
    return ''.join(out)


def write_catalog(path: Path, meta: dict, found: dict, translations: dict, obsolete: dict):
    lines = ['# risen-modkit language file (docs/editor/LOCALIZATION.md). The English text is the key; an empty value means "not translated yet".',
             '# python tools/i18n.py update <code> adds new texts, python tools/i18n.py check <code> verifies placeholders.', '',
             '[meta]']
    for k in ('code', 'name', 'english_name', 'author'):
        if k in meta:
            lines.append(f'{k} = {q(str(meta[k]))}')
    lines += ['', '[strings]']
    for key, where in found.items():
        lines.append(f'# {where[0]}' + (f' (+{len(where) - 1})' if len(where) > 1 else ''))
        lines.append(f'{q(key)} = {q(translations.get(key, ""))}')
    if obsolete:
        lines += ['', '# Texts the editor no longer uses (delete them once nothing needs them).', '[obsolete]']
        for key, value in obsolete.items():
            lines.append(f'{q(key)} = {q(value)}')
    path.write_text('\n'.join(lines) + '\n', encoding='utf-8', newline='\n')


SPEC = re.compile(r'%(?:[-+#0]*)(?:\d+|\*)?(?:\.(?:\d+|\*))?(?:hh|h|ll|l|z|j|t|L)?[diouxXeEfFgGaAcspn%]')
HOLE = re.compile(r'\{(\d+)\}')


def signature(s: str):
    specs = sorted(x for x in SPEC.findall(s) if x != '%%')
    holes = sorted(set(HOLE.findall(s.replace('{{', '').replace('}}', ''))))
    return specs, holes


def load(path: Path):
    with open(path, 'rb') as f:
        return tomllib.load(f)


def lang_files(codes):
    if codes:
        return [LANG / f'{c}.toml' for c in codes]
    return sorted(p for p in LANG.glob('*.toml') if p.name != 'template.toml')


def cmd_extract(args):
    found = scan()
    write_catalog(LANG / 'template.toml', {'code': '', 'name': '', 'english_name': ''}, found, {}, {})
    print(f'{len(found)} texts -> {LANG / "template.toml"}')


def cmd_update(args):
    found = scan()
    for p in lang_files(args.codes):
        data = load(p) if p.exists() else {'meta': {'code': p.stem, 'name': p.stem, 'english_name': p.stem}}
        old = dict(data.get('strings', {}))
        obsolete = dict(data.get('obsolete', {}))
        gone = {k: v for k, v in old.items() if k not in found and v}
        obsolete.update(gone)
        for k in list(obsolete):
            if k in found:  # came back
                old.setdefault(k, obsolete.pop(k))
        write_catalog(p, data.get('meta', {}), found, old, obsolete)
        done = sum(1 for k in found if old.get(k))
        print(f'{p.name}: {done}/{len(found)} translated, {len(gone)} became obsolete')


def cmd_check(args):
    found = scan()
    bad = 0
    missing_total = 0
    for p in lang_files(args.codes):
        data = load(p)
        meta = data.get('meta', {})
        strings = data.get('strings', {})
        if not meta.get('code'):
            print(f'{p.name}: [meta] code missing')
            bad += 1
        missing = [k for k in found if not strings.get(k)]
        unknown = [k for k in strings if k not in found]
        broken = []
        for k, v in strings.items():
            if not v:
                continue
            if signature(k) != signature(v):
                broken.append((k, v))
        print(f'{p.name}: {len(found) - len(missing)}/{len(found)} translated, {len(unknown)} unknown, {len(broken)} broken')
        for k, v in broken:
            print(f'  BROKEN placeholders: {k!r} -> {v!r}')
            bad += 1
        if args.verbose:
            for k in missing:
                print(f'  missing: {k!r}')
            for k in unknown:
                print(f'  unknown: {k!r}')
        missing_total += len(missing)
    if bad:
        return 1
    if args.strict and missing_total:
        return 2
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('command', choices=['extract', 'update', 'check'])
    ap.add_argument('codes', nargs='*')
    ap.add_argument('--strict', action='store_true', help='check: fail when texts are missing')
    ap.add_argument('-v', '--verbose', action='store_true')
    args = ap.parse_args()
    sys.exit({'extract': cmd_extract, 'update': cmd_update, 'check': cmd_check}[args.command](args) or 0)


if __name__ == '__main__':
    main()
