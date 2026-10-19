import importlib.util
import tempfile
import unittest
import json
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from types import SimpleNamespace
from unittest.mock import patch
from pathlib import Path

spec = importlib.util.spec_from_file_location('experiment', Path(__file__).resolve().parents[1] / 'tools' / 'experiment.py')
experiment = importlib.util.module_from_spec(spec)
spec.loader.exec_module(experiment)


def event(name, ms, kind=3, boot='a', **kw):
    row = dict(mode=4, node='1234', boot=boot, ms=ms, event=name, kind=kind, id='4567',
               chunk=65535, bytes=400, bitmap='0', window=0, rto_ms=0, airtime_ms=0, crc32='12345678')
    row.update(kw)
    return row


class ExperimentTests(unittest.TestCase):
    def test_loss_and_incomplete_transfers_are_not_hidden(self):
        rows = experiment.summarise([
            event('START', 100), event('TX_START', 120),
            event('TX_DONE', 200, 8, chunk=0, bytes=230, airtime_ms=1574),
            event('TIMEOUT', 500),
            event('TX_DONE', 600, 8, chunk=0, bytes=230, airtime_ms=1574),
            event('GROUP', 610, window=4), event('DELIVERED', 1000),
            event('START', 1200, id='another'),
        ])
        self.assertEqual(rows[0]['elapsed_ms'], 900)
        self.assertEqual(rows[0]['radio_elapsed_ms'], 880)
        self.assertEqual(rows[0]['repeated_chunks'], 1)
        self.assertEqual(rows[0]['repeated_data_bytes'], 180)
        self.assertEqual(rows[0]['timeouts'], 1)
        self.assertEqual(rows[0]['tx_airtime_ms'], 3148)
        self.assertEqual(rows[1]['status'], 'incomplete')

    def test_reboots_are_separate_and_wrap_is_valid(self):
        rows = experiment.summarise([event('START', 0xfffffff0), event('DELIVERED', 32),
                                     event('RESUME', 10, boot='b'), event('DELIVERED', 110, boot='b')])
        self.assertEqual(rows[0]['elapsed_ms'], 48)
        self.assertEqual(rows[1]['elapsed_ms'], 100)
        self.assertTrue(rows[1]['resumed'])

    def test_serial_noise_and_truncated_rows(self):
        row = event('START', 100)
        line = '[EXP],' + ','.join(str(row[f]) for f in experiment.FIELDS)
        parsed = list(experiment.parse_lines(['boot noise', '[EXP],4,truncated', '12:00:00 -> ' + line]))
        self.assertEqual(len(parsed), 1)
        self.assertEqual(parsed[0]['mode'], 4)
        self.assertEqual(parsed[0]['crc32'], '12345678')

    def test_prepare_uses_application_function(self):
        with tempfile.TemporaryDirectory() as folder:
            dest = Path(folder) / 'prepare.html'
            experiment.prepare_page(type('Args', (), {'out': str(dest)})())
            html = dest.read_text()
            self.assertIn('async function compressPhoto(file)', html)
            self.assertIn('blob.size <= 8192', html)
            self.assertNotIn('async function attachPhoto', html)

    def test_upload_keeps_bytes_and_reuses_id_after_lost_response(self):
        received = []

        class Handler(BaseHTTPRequestHandler):
            def log_message(self, *args):
                pass

            def do_GET(self):
                self.send_response(200)
                self.end_headers()
                self.wfile.write(b'{"role":"civilian","radio":true,"storage":true,"transferMode":4,"node":"1234"}')

            def do_POST(self):
                body = self.rfile.read(int(self.headers['Content-Length']))
                received.append((self.path, body))
                self.send_response(503 if len(received) == 1 else 201)
                self.end_headers()
                self.wfile.write(b'{}')

        server = ThreadingHTTPServer(('127.0.0.1', 0), Handler)
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            with tempfile.TemporaryDirectory() as folder:
                jpeg = Path(folder) / 'photo.jpg'
                payload = b'\xff\xd8' + bytes(range(256)) + b'\xff\xd9'
                jpeg.write_bytes(payload)
                args = SimpleNamespace(jpeg=str(jpeg), base=f'http://127.0.0.1:{server.server_port}',
                                       out=str(Path(folder) / 'trial.json'), mode=4, scenario='mock-direct', insecure=False)
                with patch.object(experiment.time, 'sleep', lambda _: None):
                    experiment.upload(args)
                self.assertEqual(received[0], received[1])
                self.assertEqual(received[2][1], payload)
                trial = json.loads(Path(args.out).read_text())
                self.assertEqual(trial['jpeg_bytes'], len(payload))
                self.assertEqual(trial['jpeg_sha256'], experiment.hashlib.sha256(payload).hexdigest())
        finally:
            server.shutdown()
            server.server_close()
            thread.join()


if __name__ == '__main__':
    unittest.main()
