"""Upload identical JPEG bytes and summarise instrumented Serial logs. Python stdlib only."""
import argparse
import csv
import hashlib
import json
import secrets
import ssl
import time
import urllib.error
import urllib.request
import zlib
from pathlib import Path

FIELDS = ['mode', 'node', 'boot', 'ms', 'event', 'kind', 'id', 'chunk', 'bytes',
          'bitmap', 'window', 'rto_ms', 'airtime_ms', 'crc32']


def parse_lines(lines):
    for line in lines:
        start = line.find('[EXP],')
        if start < 0:
            continue
        values = next(csv.reader([line[start + 6:].strip()]))
        if len(values) != len(FIELDS):
            continue  # Truncated serial lines are never used as measurements.
        row = dict(zip(FIELDS, values))
        try:
            for name in ('mode', 'ms', 'kind', 'chunk', 'bytes', 'window', 'rto_ms', 'airtime_ms'):
                row[name] = int(row[name])
            int(row['bitmap'], 16)
        except ValueError:
            continue
        yield row


def summarise(events):
    trials = {}
    for e in events:
        kind = 3 if e['kind'] in (4, 8, 9) else e['kind']
        key = e['mode'], e['node'], e['boot'], kind, e['id']
        if e['event'] in ('START', 'RESUME'):
            if key in trials:
                continue
            trials[key] = dict(mode=e['mode'], node=e['node'], boot=e['boot'], kind=kind,
                               id=e['id'], bytes=e['bytes'], crc32=e['crc32'],
                               resumed=e['event'] == 'RESUME', start_ms=e['ms'],
                               status='incomplete', elapsed_ms='', first_tx_ms='', radio_elapsed_ms='', tx_frames=0,
                               repeated_chunks=0, repeated_data_bytes=0, timeouts=0,
                               tx_airtime_ms=0, tx_failures=0, max_group=0, _chunks=set())
        trial = trials.get(key)
        if trial is None:
            continue
        if e['event'] == 'TX_START' and trial['first_tx_ms'] == '':
            trial['first_tx_ms'] = e['ms']
        elif e['event'] == 'TX_DONE':
            trial['tx_frames'] += 1
            trial['tx_airtime_ms'] += e['airtime_ms']
            if e['kind'] in (4, 8):
                if e['chunk'] in trial['_chunks']:
                    trial['repeated_chunks'] += 1
                    trial['repeated_data_bytes'] += max(0, e['bytes'] - 50)  # 39 header + 8 HMAC + 3 chunk prefix
                trial['_chunks'].add(e['chunk'])
        elif e['event'] == 'TX_FAIL':
            trial['tx_failures'] += 1
        elif e['event'] == 'TIMEOUT':
            trial['timeouts'] += 1
        elif e['event'] == 'GROUP':
            trial['max_group'] = max(trial['max_group'], e['window'])
        elif e['event'] == 'DELIVERED' and trial['status'] != 'delivered':
            trial['status'] = 'delivered'
            trial['elapsed_ms'] = (e['ms'] - trial['start_ms']) & 0xffffffff
            if trial['first_tx_ms'] != '':
                trial['radio_elapsed_ms'] = (e['ms'] - trial['first_tx_ms']) & 0xffffffff
    return [{k: v for k, v in t.items() if not k.startswith('_')} for t in trials.values()]


def write_summary(args):
    # Process each capture chronologically. A fresh boot is a separate segment;
    # uptime is not a wall clock and cannot measure downtime across restarts.
    events = []
    for filename in args.logs:
        events.extend(parse_lines(Path(filename).read_text(encoding='utf-8-sig', errors='replace').splitlines()))
    rows = summarise(events)
    if not rows:
        raise ValueError('No START/RESUME events found. Begin capture before submitting a new report.')
    with Path(args.out).open('x', newline='', encoding='utf-8') as f:
        writer = csv.DictWriter(f, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)
    print(f'Saved {len(rows)} transfer segments to {args.out}. Incomplete and resumed transfers remain labelled.')


def upload(args):
    data = Path(args.jpeg).read_bytes()
    if not 4 <= len(data) <= 8192 or not data.startswith(b'\xff\xd8') or not data.endswith(b'\xff\xd9'):
        raise ValueError('Choose a prepared JPEG of at most 8192 bytes. This tool does not recompress it.')
    base = args.base.rstrip('/')
    if not base.startswith(('http://', 'https://')):
        raise ValueError('--base must start with http:// or https://')
    context = ssl._create_unverified_context() if args.insecure else ssl.create_default_context()
    session, report_id = secrets.token_hex(16), secrets.token_hex(8)

    def request(path, body=None, binary=False):
        headers = {'X-Session': session}
        if body is not None:
            headers['Content-Type'] = 'image/jpeg' if binary else 'application/json'
            payload = body if binary else json.dumps(body).encode()
        else:
            payload = None
        last_error = None
        for attempt in range(3):
            try:
                req = urllib.request.Request(base + path, data=payload, headers=headers)
                with urllib.request.urlopen(req, context=context, timeout=15) as response:
                    return json.load(response)
            except urllib.error.HTTPError as error:
                detail = error.read().decode(errors='replace')
                if error.code < 500 or error.code == 507:
                    raise ValueError(f'Node returned {error.code}: {detail}') from error
                last_error = error
            except (urllib.error.URLError, TimeoutError) as error:
                last_error = error
            time.sleep(attempt + 1)
        raise ValueError(f'Connection failed after retries: {last_error}')

    status = request('/api/status')
    if status.get('role') != 'civilian' or not status.get('storage') or not status.get('radio'):
        raise ValueError('Connect to a civilian node with storage and radio ready.')
    if args.mode and status.get('transferMode') != args.mode:
        raise ValueError(f"Expected mode {args.mode}; node reports {status.get('transferMode')}")
    trial = dict(report=report_id, session=session, base=base, mode=status.get('transferMode'),
                 node=status.get('node'), scenario=args.scenario, jpeg_bytes=len(data),
                 jpeg_sha256=hashlib.sha256(data).hexdigest(), jpeg_crc32=f'{zlib.crc32(data):08x}',
                 started_utc=time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime()))
    output = Path(args.out)
    with output.open('x', encoding='utf-8') as f:
        json.dump(trial, f, indent=2)
    # Both retries reuse the exact logical IDs. No test history is erased.
    body = dict(id=report_id, name='Transfer experiment', location=args.scenario[:47],
                text='Controlled image transfer test', people=1, flags=0, timestamp=int(time.time()))
    request('/api/sos', body)
    request('/api/image?id=' + report_id, data, True)
    print(f"Queued {len(data)} identical JPEG bytes in mode {trial['mode']}; report {report_id}.")
    print(f'SHA-256: {trial["jpeg_sha256"]}\nTrial details: {output}')


def prepare_page(args):
    # Reuse the actual application function, avoiding a second compression algorithm.
    source = (Path(__file__).resolve().parents[1] / 'data' / 'app.js').read_text(encoding='utf-8')
    compression = source[source.index('async function compressPhoto('):source.index('async function attachPhoto(')]
    html = '''<!doctype html><meta charset="utf-8"><title>Prepare one test photo</title>
<style>body{font:18px system-ui;max-width:660px;margin:50px auto;padding:20px}button,input{font:inherit;margin:12px 0}img{image-rendering:pixelated;max-width:100%}</style>
<h1>Prepare one test photo</h1><p>Choose a photo once, then reuse the downloaded JPEG for every mode. Everything runs in this browser.</p>
<input id="file" type="file" accept="image/*"><button id="prepare">Prepare JPEG</button><p id="status"></p><a id="download" hidden>Download test JPEG</a><p><img id="preview" alt=""></p><script>
'''
    html += compression
    html += '''
let previous;
document.getElementById('prepare').onclick=async()=>{
const status=document.getElementById('status');
try{const blob=await compressPhoto(document.getElementById('file').files[0]);
if(previous)URL.revokeObjectURL(previous);previous=URL.createObjectURL(blob);
const a=document.getElementById('download');a.href=previous;a.download='test-photo.jpg';a.hidden=false;
document.getElementById('preview').src=previous;status.textContent=blob.size+' bytes. Save this file and reuse it unchanged.';
}catch(e){status.textContent=e.message;}};</script>'''
    Path(args.out).write_text(html, encoding='utf-8')
    print(f'Open {Path(args.out).resolve()} in your browser.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(required=True)
    p = sub.add_parser('prepare', help='Create an offline photo preparation page using the application compressor')
    p.add_argument('--out', default='prepare-test-photo.html')
    p.set_defaults(run=prepare_page)
    p = sub.add_parser('upload', help='Upload the same prepared JPEG bytes with a fresh test SOS')
    p.add_argument('--base', default='https://192.168.4.1')
    p.add_argument('--jpeg', required=True)
    p.add_argument('--out', required=True, help='New private trial JSON filename')
    p.add_argument('--scenario', default='direct-clear')
    p.add_argument('--mode', type=int, choices=range(1, 5))
    p.add_argument('--insecure', action='store_true', help='Accept the local board self-signed HTTPS certificate')
    p.set_defaults(run=upload)
    p = sub.add_parser('summarise', help='Convert Serial captures into one row per transfer and boot')
    p.add_argument('logs', nargs='+')
    p.add_argument('--out', required=True)
    p.set_defaults(run=write_summary)
    args = parser.parse_args()
    try:
        args.run(args)
    except (OSError, ValueError) as error:
        parser.exit(1, f'{error}\n')


if __name__ == '__main__':
    main()
