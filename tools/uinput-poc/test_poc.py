import json
from pathlib import Path
import select
import signal
import subprocess
import sys
import time
import unittest
import urllib.error
import urllib.request

SCRIPT = Path(__file__).with_name('poc.py')


class HelperTests(unittest.TestCase):
    def setUp(self):
        self.process = subprocess.Popen(
            [sys.executable, str(SCRIPT), '--helper', '--simulate'],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            text=True)
        self.assertTrue(self.read()['ready'])

    def tearDown(self):
        if self.process.poll() is None:
            self.process.stdin.close()
            self.process.wait(timeout=2)
        for stream in (self.process.stdin, self.process.stdout, self.process.stderr):
            stream.close()

    def read(self):
        self.assertTrue(select.select([self.process.stdout], [], [], 2)[0])
        return json.loads(self.process.stdout.readline())

    def send(self, op, ms=None):
        self.process.stdin.write(json.dumps({'op': op, 'ms': ms}) + '\n')
        self.process.stdin.flush()
        return self.read()

    def finish(self):
        self.process.stdin.close()
        self.process.wait(timeout=2)
        self.assertEqual(self.process.returncode, 0)
        return [json.loads(line)['event'] for line in self.process.stderr.readlines()]

    def test_expiry_releases_without_browser_or_controller_request(self):
        self.assertTrue(self.send('hold', 60)['held'])
        time.sleep(.15)
        self.assertFalse(self.send('status')['held'])
        self.assertEqual(self.finish(), [1, 0])

    def test_disconnect_releases(self):
        self.send('hold', 30000)
        self.assertEqual(self.finish(), [1, 0])

    def test_signal_releases(self):
        self.send('hold', 30000)
        self.process.send_signal(signal.SIGTERM)
        self.process.wait(timeout=2)
        self.assertEqual(self.finish(), [1, 0])

    def test_release_is_idempotent(self):
        self.send('hold', 30000)
        self.assertFalse(self.send('release')['held'])
        self.assertFalse(self.send('release')['held'])
        self.assertEqual(self.finish(), [1, 0])

    def test_duplicate_cannot_extend_hold(self):
        self.send('hold', 60)
        self.assertIn('error', self.send('hold', 30000))
        time.sleep(.15)
        self.assertFalse(self.send('status')['held'])
        self.assertEqual(self.finish(), [1, 0])

    def test_reject_invalid_duration(self):
        for value in (0, -1, 30001, True, '100', None):
            self.assertIn('error', self.send('hold', value))
        self.assertEqual(self.finish(), [])

    def test_bad_message_does_not_lose_cleanup(self):
        self.send('hold', 30000)
        self.process.stdin.write('not json\n')
        self.process.stdin.flush()
        self.assertIn('error', self.read())
        self.assertEqual(self.finish(), [1, 0])

    def test_partial_input_does_not_block_expiry(self):
        self.send('hold', 60)
        self.process.stdin.write('{')
        self.process.stdin.flush()
        time.sleep(.15)
        self.process.stdin.write('"op":"status"}\n')
        self.process.stdin.flush()
        self.assertFalse(self.read()['held'])
        self.assertEqual(self.finish(), [1, 0])


class WebTests(unittest.TestCase):
    def test_authenticated_web_hold_and_controller_crash(self):
        process = subprocess.Popen(
            [sys.executable, '-B', str(SCRIPT), '--simulate', '--port', '0'],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        try:
            lines = []
            while len(lines) < 3:
                line = process.stdout.readline()
                self.assertTrue(line)
                lines.append(line.strip())
            url, token = lines[2].removeprefix('Local URL: ').split('#')
            with urllib.request.urlopen(url, timeout=2) as response:
                self.assertIn(b'Hold push-to-talk', response.read())

            def request(command, auth):
                req = urllib.request.Request(url + 'api', json.dumps(command).encode(),
                    {'Authorization': 'Bearer ' + auth, 'Content-Type': 'application/json'})
                return urllib.request.urlopen(req, timeout=2)

            with self.assertRaises(urllib.error.HTTPError) as error:
                request({'op': 'hold', 'ms': 100}, 'wrong')
            self.assertEqual(error.exception.code, 403)
            error.exception.close()
            with request({'op': 'status'}, token) as response:
                self.assertFalse(json.load(response)['held'])
            with request({'op': 'hold', 'ms': 30000}, token) as response:
                self.assertTrue(json.load(response)['held'])
            # Kill the controller, not the helper: EOF must release immediately.
            process.kill()
            process.wait(timeout=2)
            self.assertTrue(select.select([process.stderr], [], [], 2)[0])
            self.assertEqual(json.loads(process.stderr.readline())['event'], 1)
            self.assertTrue(select.select([process.stderr], [], [], 2)[0])
            self.assertEqual(json.loads(process.stderr.readline())['event'], 0)
        finally:
            if process.poll() is None:
                process.terminate()
                process.wait(timeout=2)
            process.stdout.close()
            process.stderr.close()


if __name__ == '__main__':
    unittest.main()
