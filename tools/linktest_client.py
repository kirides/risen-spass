#!/usr/bin/env python3
"""Client of the live link for the `link` scenario (scripts/test-run.ps1 -LinkClient starts it): talks to the game over the named pipe.

    python -I tools/linktest_client.py <bin\\spass dir> <result.json>

Waits for <spass>\\link.json, connects to the pipe, runs a series of requests and writes {check name: [ok, detail]} to the result file.
Windows only (message-mode named pipe through ctypes); the standard library is enough.
"""
import ctypes
import json
import os
import sys
import time


def wait_for(path, seconds):
    end = time.time() + seconds
    while time.time() < end:
        if os.path.exists(path):
            try:
                with open(path, encoding='utf-8') as f:
                    return json.load(f)
            except (OSError, ValueError):
                pass
        time.sleep(0.5)
    return None


class Link:
    def __init__(self, pipe):
        import msvcrt
        k32 = ctypes.windll.kernel32
        k32.CreateFileW.restype = ctypes.c_void_p
        k32.CreateFileW.argtypes = [ctypes.c_wchar_p, ctypes.c_ulong, ctypes.c_ulong, ctypes.c_void_p, ctypes.c_ulong, ctypes.c_ulong, ctypes.c_void_p]
        k32.SetNamedPipeHandleState.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p]
        k32.ReadFile.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_ulong, ctypes.c_void_p, ctypes.c_void_p]
        k32.PeekNamedPipe.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_ulong, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p]
        end = time.time() + 60
        while True:
            handle = k32.CreateFileW(pipe, 0xC0000000, 0, None, 3, 0, None)  # GENERIC_READ | GENERIC_WRITE, OPEN_EXISTING
            if handle not in (None, ctypes.c_void_p(-1).value):
                break
            if time.time() > end:
                raise OSError('cannot open ' + pipe)
            time.sleep(0.3)
        mode = ctypes.c_ulong(2)  # PIPE_READMODE_MESSAGE: one read = one message
        if not k32.SetNamedPipeHandleState(handle, ctypes.byref(mode), None, None):
            raise OSError('SetNamedPipeHandleState failed: %d' % ctypes.GetLastError())
        import os as _os
        self.f = _os.fdopen(msvcrt.open_osfhandle(handle, _os.O_RDWR | _os.O_BINARY), 'r+b', buffering=0)
        self.handle = ctypes.c_void_p(handle)
        self.q = []
        self.trace = []
        self.next_id = 1
        self.events = []

    def _read_one(self, timeout):
        """Next message or None (timeout) / False (pipe closed). A blocking read on a synchronous handle would block writes of other threads, so we only
        read what PeekNamedPipe says is there."""
        end = time.time() + timeout
        k32 = ctypes.windll.kernel32
        while time.time() < end:
            avail = ctypes.c_ulong(0)
            left = ctypes.c_ulong(0)
            if not k32.PeekNamedPipe(self.handle, None, 0, None, ctypes.byref(avail), ctypes.byref(left)):
                return False
            if avail.value > 0:
                data = b''
                buf = ctypes.create_string_buffer(65536)
                while True:
                    got = ctypes.c_ulong(0)
                    ok = k32.ReadFile(self.handle, buf, 65536, ctypes.byref(got), None)
                    data += buf.raw[:got.value]
                    if ok:
                        break  # the message is complete
                    if ctypes.GetLastError() != 234:  # ERROR_MORE_DATA: the rest of the same message follows
                        return False
                try:
                    msg = json.loads(data.decode('utf-8'))
                    self.trace.append(('msg', len(data), msg.get('id'), msg.get('event')))
                    return msg
                except ValueError:
                    self.trace.append(('bad', len(data), data[:120]))
                    return {'ok': False, 'error': 'client: reply is not JSON'}
            time.sleep(0.01)
        return None

    def send_raw(self, raw):
        self.f.write(raw)

    def request(self, cmd, args=None, timeout=20):
        rid = self.next_id
        self.next_id += 1
        self.send_raw(json.dumps({'id': rid, 'cmd': cmd, 'args': args or {}}).encode('utf-8'))
        return self.wait_reply(rid, timeout)

    def wait_reply(self, rid, timeout):
        end = time.time() + timeout
        while time.time() < end:
            msg = self._read_one(0.5)
            if msg is None:
                continue
            if msg is False:
                return {'ok': False, 'error': 'pipe closed'}
            if 'event' in msg:
                self.events.append(msg)
                continue
            if msg.get('id') == rid or (rid is None and 'id' not in msg):
                return msg
        return {'ok': False, 'error': 'timeout'}

    def wait_event(self, name, timeout):
        end = time.time() + timeout
        while time.time() < end:
            for e in self.events:
                if e['event'] == name:
                    return e
            msg = self._read_one(0.3)
            if msg is None:
                continue
            if msg is False:
                return None
            if 'event' in msg:
                self.events.append(msg)
        return None


def main():
    spass, out = sys.argv[1], sys.argv[2]
    results = {}

    def check(name, ok, detail=''):
        results[name] = [bool(ok), str(detail)[:300]]

    info = wait_for(os.path.join(spass, 'link.json'), 180)
    check('link.json written', info is not None and info.get('protocol') == 1 and info.get('pipe', '').startswith('\\\\.\\pipe\\'), info)
    if info is None:
        json.dump(results, open(out, 'w'))
        return
    link = Link(info['pipe'])

    r = link.request('hello')
    check('hello', r.get('ok') and r['result']['protocol'] == 1 and r['result']['pid'] == info['pid'] and r['result']['pipe'] == info['pipe'], r)
    check('ping', link.request('ping').get('result', {}).get('pong') is True)
    r = link.request('mods')
    mods = {m['id']: m for m in r.get('result', {}).get('mods', [])}
    check('mods', r.get('ok') and mods.get('link_target', {}).get('state') == 'loaded' and 'link_target' in r['result']['order'], list(mods))
    r = link.request('commands')
    check('commands', 'link_target.echo' in r.get('result', {}).get('commands', []) and 'reload' in r['result']['commands'], r)

    r = link.request('link_target.echo', {'x': 1, 'list': [1, 2, 3], 's': 'hä'})
    check('mod command', r.get('ok') and r['result'] == {'x': 1, 'n': 3, 's': 'hä', 'mod': 'link_target'}, r)
    r = link.request('link_target.fail')
    check('mod command error', r.get('ok') is False and r.get('error') == 'boom', r)
    r = link.request('link_target.crash')
    check('mod command Lua error', r.get('ok') is False and 'oops' in r.get('error', ''), r)
    r = link.request('no.such')
    check('unknown command', r.get('ok') is False and 'unknown command' in r.get('error', ''), r)
    link.send_raw(b'this is not json')
    r = link.wait_reply(None, 10)
    check('malformed request', r.get('ok') is False and 'JSON' in r.get('error', ''), r)
    r = link.request('reload', {})
    check('bad arguments', r.get('ok') is False and 'args.mod' in r.get('error', ''), r)
    big = 'y' * 200000
    r = link.request('link_target.echo', {'s': big})
    check('big message', r.get('ok') and len(r['result']['s']) == len(big), r.get('error', ''))

    r = link.request('subscribe', {'log': 'info'})
    check('subscribe', r.get('ok') and r['result']['log'] == 'info', r)
    r = link.request('subscribe', {'log': 'loud'})
    check('subscribe bad level', r.get('ok') is False, r)
    r = link.request('reload', {'mod': 'link_target'})
    acts = r.get('result', {}).get('reports', [{}])[0].get('actions', [])
    check('reload', r.get('ok') and acts and acts[0]['action'] == 'reloaded' and acts[0]['id'] == 'link_target', r)
    check('log events', link.wait_event('log', 10) is not None, len(link.events))
    r = link.request('subscribe', {'log': 'off'})
    r = link.request('link_target.echo', {'x': 2})
    check('command after reload', r.get('ok') and r['result']['n'] == 0 and r['result']['x'] == 2, r)
    r = link.request('reload', {'mod': 'no_such_mod'})
    check('reload unknown mod', r.get('ok') is False, r)

    link.events.clear()
    r = link.request('link_target.push')
    ev = link.wait_event('link_target.hello', 10)
    check('event from a mod', r.get('ok') and ev is not None and ev['data'] == {'n': 1, 'text': 'hi'}, ev)

    results['trace'] = [True, repr(link.trace[-60:])]
    json.dump(results, open(out, 'w'), indent=1)
    time.sleep(1)  # the scenario now waits for `link.disconnected`
    link.f.close()


if __name__ == '__main__':
    main()
