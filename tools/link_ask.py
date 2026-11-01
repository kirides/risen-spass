"""Ask the running game one live-link command and print the reply: python tools/link_ask.py <bin\\spass> <cmd> [json args]

A small helper for looking at what the layer answers (docs/LUA_API.md "Live link"); the pipe is found through link.json."""
import json
import os
import sys


def main():
    folder, cmd = sys.argv[1], sys.argv[2]
    args = json.loads(sys.argv[3]) if len(sys.argv) > 3 else {}
    info = json.load(open(os.path.join(folder, 'link.json')))
    pipe = open(info['pipe'], 'r+b', buffering=0)
    pipe.write(json.dumps({'id': 1, 'cmd': cmd, 'args': args}).encode())
    print(json.dumps(json.loads(pipe.read(4 * 1024 * 1024)), indent=1))


main()
