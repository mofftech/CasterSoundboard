#!/usr/bin/env python3
"""Standalone Linux keyboard experiment; no Caster profile or audio changes."""
import argparse
import ctypes as C
import ctypes.util
import hmac
import json
import os
from pathlib import Path
import secrets
import select
import signal
import subprocess
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

MAX_HOLD_MS = 30000


class Keyboard:
    def __init__(self):
        self.device = C.c_void_p()
        self.ui = C.c_void_p()
        self.lib = C.CDLL(ctypes.util.find_library('evdev') or 'libevdev.so.2')
        signatures = {
            'libevdev_new': ([], C.c_void_p),
            'libevdev_free': ([C.c_void_p], None),
            'libevdev_set_name': ([C.c_void_p, C.c_char_p], None),
            'libevdev_enable_event_code': ([C.c_void_p, C.c_uint, C.c_uint, C.c_void_p], C.c_int),
            'libevdev_uinput_create_from_device': ([C.c_void_p, C.c_int, C.POINTER(C.c_void_p)], C.c_int),
            'libevdev_uinput_write_event': ([C.c_void_p, C.c_uint, C.c_uint, C.c_int], C.c_int),
            'libevdev_uinput_get_devnode': ([C.c_void_p], C.c_char_p),
            'libevdev_uinput_destroy': ([C.c_void_p], None),
            'libevdev_event_code_from_name': ([C.c_uint, C.c_char_p], C.c_int),
        }
        for name, (args, result) in signatures.items():
            fn = getattr(self.lib, name)
            fn.argtypes, fn.restype = args, result
        self.device = self.lib.libevdev_new()
        if not self.device:
            raise MemoryError('libevdev_new')
        try:
            self.lib.libevdev_set_name(self.device, b'Caster uinput POC keyboard')
            # Advertise a normal keyboard for udev classification, but the helper
            # accepts commands for only the key fixed at startup. No physical input reads.
            for key in range(1, 249):
                self.check(self.lib.libevdev_enable_event_code(self.device, 1, key, None))
            self.check(self.lib.libevdev_uinput_create_from_device(self.device, -2, C.byref(self.ui)))
        except BaseException:
            self.close()
            raise

    @staticmethod
    def check(result):
        if result < 0:
            raise OSError(-result, os.strerror(-result))

    def code(self, name):
        value = self.lib.libevdev_event_code_from_name(1, name.encode('ascii'))
        if not 1 <= value < 249:
            raise ValueError('Use a standard Linux KEY_* keyboard code, e.g. KEY_V')
        return value

    def event(self, key, value):
        self.check(self.lib.libevdev_uinput_write_event(self.ui, 1, key, value))
        self.check(self.lib.libevdev_uinput_write_event(self.ui, 0, 0, 0))

    def node(self):
        return self.lib.libevdev_uinput_get_devnode(self.ui).decode()

    def close(self):
        if self.ui:
            self.lib.libevdev_uinput_destroy(self.ui)
            self.ui = C.c_void_p()
        if self.device:
            self.lib.libevdev_free(self.device)
            self.device = C.c_void_p()


class FakeKeyboard:
    """Explicit simulation for lifecycle tests; never opens an input device."""
    def code(self, name):
        return 47

    def event(self, key, value):
        print(json.dumps({'event': value}), file=sys.stderr, flush=True)

    def node(self):
        return 'SIMULATION'

    def close(self):
        pass


def helper(args):
    keyboard = FakeKeyboard() if args.simulate else Keyboard()
    held = False
    deadline = 0.0
    running = True

    def stop(*_):
        nonlocal running
        running = False

    def release():
        nonlocal held
        if held:
            keyboard.event(key, 0)
            held = False

    def reply(value):
        print(json.dumps(value), flush=True)

    signal.signal(signal.SIGTERM, stop)
    signal.signal(signal.SIGINT, stop)
    try:
        key = keyboard.code(args.key)
        # Device enumeration is asynchronous. This conservative settling delay
        # is a POC measure, not proof that a compositor has accepted the device.
        if not args.simulate:
            time.sleep(1.5)
        reply({'ready': True, 'node': keyboard.node(), 'key': args.key})
        pending = b''
        while running:
            if held and time.monotonic() >= deadline:
                release()
            readable, _, _ = select.select([sys.stdin.fileno()], [], [], 0.02)
            if not readable:
                continue
            chunk = os.read(sys.stdin.fileno(), 4096)
            if not chunk:
                break
            pending += chunk
            if len(pending) > 4096:
                raise ValueError('Oversized helper command')
            while b'\n' in pending:
                line, pending = pending.split(b'\n', 1)
                try:
                    command = json.loads(line)
                    op = command.get('op')
                    if op == 'hold':
                        duration = command.get('ms')
                        if type(duration) is not int or not 1 <= duration <= MAX_HOLD_MS:
                            raise ValueError('Hold must be 1..30000 ms')
                        if held:
                            raise ValueError('Already holding; release before another test')
                        # Record ownership before writing, including a partial write failure.
                        held = True
                        deadline = time.monotonic() + duration / 1000
                        keyboard.event(key, 1)
                    elif op == 'release':
                        release()
                    elif op != 'status':
                        raise ValueError('Unknown operation')
                    reply({'held': held, 'remaining_ms': max(0, int((deadline - time.monotonic()) * 1000)) if held else 0})
                except (ValueError, TypeError, AttributeError) as exc:
                    reply({'error': str(exc)})
    finally:
        try:
            release()
        finally:
            keyboard.close()


class Controller:
    def __init__(self, args):
        command = [sys.executable, str(Path(__file__).resolve()), '--helper', '--key', args.key]
        if args.simulate:
            command.append('--simulate')
        self.lock = threading.Lock()
        self.process = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
        try:
            self.ready = self.read()
        except BaseException:
            self.close()
            raise

    def read(self):
        if not select.select([self.process.stdout], [], [], 5)[0]:
            raise RuntimeError('Input helper did not respond')
        line = self.process.stdout.readline()
        if not line:
            raise RuntimeError('Input helper stopped; inspect terminal diagnostics')
        return json.loads(line)

    def command(self, value):
        with self.lock:
            self.process.stdin.write(json.dumps(value) + '\n')
            self.process.stdin.flush()
            return self.read()

    def close(self):
        if self.process.stdin:
            self.process.stdin.close()
        try:
            self.process.wait(timeout=2)
        except subprocess.TimeoutExpired:
            self.process.terminate()
            try:
                self.process.wait(timeout=2)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait()


def serve(args):
    controller = Controller(args)
    token = secrets.token_urlsafe(24)
    page = Path(__file__).with_name('index.html').read_bytes()

    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *_):
            pass

        def send(self, status, body, content_type='application/json'):
            if not isinstance(body, bytes):
                body = json.dumps(body).encode()
            self.send_response(status)
            self.send_header('Content-Type', content_type)
            self.send_header('Content-Length', str(len(body)))
            self.send_header('Cache-Control', 'no-store')
            self.send_header('X-Frame-Options', 'DENY')
            self.end_headers()
            self.wfile.write(body)

        def do_GET(self):
            if self.path == '/':
                return self.send(200, page, 'text/html; charset=utf-8')
            self.send(404, {'error': 'Not found'})

        def do_POST(self):
            if not hmac.compare_digest(self.headers.get('Authorization', ''), 'Bearer ' + token):
                return self.send(403, {'error': 'Open the complete test URL printed in the terminal'})
            try:
                length = int(self.headers.get('Content-Length', '0'))
                if not 0 < length <= 256:
                    raise ValueError('Invalid request size')
                self.connection.settimeout(2)
                value = json.loads(self.rfile.read(length))
                if not isinstance(value, dict):
                    raise ValueError('Expected an object')
                if self.path != '/api':
                    return self.send(404, {'error': 'Not found'})
                result = controller.command(value)
                result.update(key=args.key, simulate=args.simulate)
                self.send(400 if 'error' in result else 200, result)
            except (ValueError, OSError, RuntimeError) as exc:
                self.send(503, {'error': str(exc)})

    server = None
    try:
        server = ThreadingHTTPServer((args.bind, args.port), Handler)
        print('Virtual keyboard: ' + controller.ready['node'], flush=True)
        print(f'Open on your phone: http://<computer-LAN-IP>:{server.server_port}/#{token}', flush=True)
        print(f'Local URL: http://127.0.0.1:{server.server_port}/#{token}', flush=True)
        print('No keys sent until a test button is pressed. Ctrl+C stops and releases.', flush=True)
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        if server:
            server.server_close()
        controller.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--key', default='KEY_V', help='Linux physical key name (default KEY_V)')
    parser.add_argument('--bind', default='127.0.0.1')
    parser.add_argument('--port', type=int, default=5053)
    parser.add_argument('--helper', action='store_true', help=argparse.SUPPRESS)
    parser.add_argument('--simulate', action='store_true', help='Test lifecycle without creating a device')
    parser.add_argument('--probe', action='store_true', help='Create and inspect a keyboard without sending keys')
    args = parser.parse_args()
    if args.probe:
        keyboard = Keyboard()
        try:
            keyboard.code(args.key)
            time.sleep(1.5)
            print('Created ' + keyboard.node() + '; no keys sent', flush=True)
            subprocess.run(['udevadm', 'info', '--query=property', '--name=' + keyboard.node()], check=True)
        finally:
            keyboard.close()
    elif args.helper:
        helper(args)
    else:
        serve(args)


if __name__ == '__main__':
    try:
        main()
    except (OSError, RuntimeError, ValueError) as exc:
        sys.exit('uinput POC: ' + str(exc))
