#!/usr/bin/env python3
"""Loopback HTTP server for the `http` scenario (scripts/test-run.ps1 -HttpServer starts it, mods/http_test talks to it).

    python -I tools/httptest_server.py PORT

Binds 127.0.0.1 only, so Windows shows no firewall prompt. Endpoints:
  /text            200 "hello"
  /json            200 {"name": "risen", "list": [1, 2, 3], "nested": {"ok": true}, "nothing": null}
  /echo            200 JSON {method, path, headers, body} of the request (any method)
  /redirect        302 -> /text
  /404             404 "not here"
  /badjson         200 "{not json"
  /empty           204
  /slow            waits 6 s, then 200 "late"
"""
import json
import sys
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer


class Handler(BaseHTTPRequestHandler):
    protocol_version = 'HTTP/1.1'

    def log_message(self, fmt, *args):
        pass

    def _send(self, status, body=b'', ctype='text/plain', extra=None):
        self.send_response(status)
        self.send_header('Content-Type', ctype)
        self.send_header('Content-Length', str(len(body)))
        self.send_header('Connection', 'close')
        for k, v in (extra or {}).items():
            self.send_header(k, v)
        self.end_headers()
        if self.command != 'HEAD':
            self.wfile.write(body)

    def _handle(self):
        length = int(self.headers.get('Content-Length') or 0)
        body = self.rfile.read(length).decode('utf-8', 'replace') if length else ''
        path = self.path
        if path == '/text':
            self._send(200, b'hello')
        elif path == '/json':
            self._send(200, json.dumps({'name': 'risen', 'list': [1, 2, 3], 'nested': {'ok': True}, 'nothing': None}).encode(), 'application/json')
        elif path == '/echo':
            doc = {'method': self.command, 'path': path, 'headers': {k.lower(): v for k, v in self.headers.items()}, 'body': body}
            self._send(200, json.dumps(doc).encode(), 'application/json')
        elif path == '/redirect':
            self._send(302, b'', extra={'Location': '/text'})
        elif path == '/404':
            self._send(404, b'not here')
        elif path == '/badjson':
            self._send(200, b'{not json', 'application/json')
        elif path == '/empty':
            self._send(204)
        elif path == '/slow':
            time.sleep(6)
            try:
                self._send(200, b'late')
            except OSError:
                pass
        else:
            self._send(404, b'unknown')

    do_GET = do_POST = do_PUT = do_PATCH = do_DELETE = do_HEAD = _handle


if __name__ == '__main__':
    server = ThreadingHTTPServer(('127.0.0.1', int(sys.argv[1])), Handler)
    server.daemon_threads = True
    server.serve_forever()
