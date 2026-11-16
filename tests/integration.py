"""Run against an existing build. Hardware writes require --same-value-writes or --input-roundtrip."""
import argparse
import json
import pathlib
import subprocess
import time

parser = argparse.ArgumentParser()
parser.add_argument('executable', type=pathlib.Path)
parser.add_argument('--same-value-writes', action='store_true')
parser.add_argument('--input-roundtrip', action='store_true', help='Temporarily toggle OSD/backlight memory/touchpad and restore each in finally')
parser.add_argument('--report', type=pathlib.Path, required=True)
args = parser.parse_args()
executable = str(args.executable.resolve())

def run(*command):
    process = subprocess.run([executable, *map(str, command)], capture_output=True, timeout=22)
    assert process.stdout, f'No worker result: {process.returncode & 0xffffffff:08x}'
    return json.loads(process.stdout.decode('utf-8'))

started = time.monotonic()
before = run('--probe')
assert before['schema'] == 1 and before['windows11']
for key, state in before['features'].items():
    if state['available']:
        assert isinstance(state['value'], int), (key, state)
        if state['options']:
            assert state['value'] in [option['value'] for option in state['options']], key
    else:
        assert state['value'] is None and state['error'], key

results = {}
for key, value in [('ioctl', 2), ('charge', 99), ('volume', -1), ('its', 5), ('gpu',3), ('flip',2), ('ocCore',2001), ('presenceLogin',2), ('presenceLock',-1), ('loginDistance',3), ('lockTimer',3), ('cameraAssist',2), ('touchpad',2), ('fnCtrlSwap',2)]:
    result = run('--apply', key, value)
    assert result['ok'] is False, result
    results[f'reject-{key}'] = result

for key in ['loginAuto', 'loginDistance', 'lockAuto', 'lockDistance', 'lockTimer', 'cameraAssist']:
    current = before['features'][key]
    if current['available'] and not current.get('enabled', True):
        result = run('--apply', key, current['value'])
        assert not result['ok'], result
        results[f'disabled-{key}'] = result

for key, parent, auto in [('loginDistance', 'presenceLogin', 'loginAuto'), ('lockDistance', 'presenceLock', 'lockAuto')]:
    f = before['features']
    if all(f[k]['available'] for k in [key, parent, auto]):
        assert f[key]['enabled'] == (f[parent]['value'] == 1 and f[auto]['value'] == 0)
        if f[key]['sensorType'] == 2:
            assert f[key]['meters'] == ([0.55, 1.0, 1.5] if key == 'loginDistance' else [0.9, 1.2, 1.5])

if args.same_value_writes:
    for key in ['volume', 'brightness','gpu','flip','oc','backlight','backlightRemember','capsOsd','numOsd','touchpad','fnCtrlSwap','presenceLogin','loginAuto','loginDistance','presenceLock','lockAuto','lockDistance','lockTimer','cameraAssist','presenceVideo']:
        current = before['features'][key]
        if current['available'] and current.get('enabled', True):
            if key=='oc' and current['value']!=0:continue
            result = run('--apply', key, current['value'])
            assert result['ok'] and result['state']['value'] == current['value'], result
            results[key] = result
    # Exercise the independent refresh guard without changing the refresh rate.
    refresh = before['features']['refresh']
    if refresh['available']:
        p = subprocess.Popen([executable, '--apply', 'refresh', str(refresh['value'])], stdin=subprocess.PIPE, stdout=subprocess.PIPE)
        preview = json.loads(p.stdout.readline().decode('utf-8'))
        assert preview['preview'] and preview['ok'], preview
        p.stdin.close()  # Simulate GUI exit before confirmation.
        p.wait(timeout=20)
        reverted = json.loads(p.stdout.read().decode('utf-8'))
        assert reverted['ok'], reverted
        results['refresh-guard-disconnect'] = reverted

if args.input_roundtrip:
    for key in ['backlightRemember', 'capsOsd', 'numOsd', 'touchpad']:
        current = before['features'][key]
        if not current['available']:
            continue
        try:
            result = run('--apply', key, 1-current['value'])
            assert result['ok'] and result['state']['value'] == 1-current['value'], result
            results[f'roundtrip-{key}'] = result
        finally:
            restored = run('--apply', key, current['value'])
            assert restored['ok'], restored
            results[f'restore-{key}'] = restored

after = run('--probe')
before_values = {k: v['value'] for k, v in before['features'].items()}
after_values = {k: v['value'] for k, v in after['features'].items()}
assert before_values == after_values, {'before': before_values, 'after': after_values}
report = {'ok': True, 'elapsedSeconds': round(time.monotonic() - started, 3), 'sameValueWritesEnabled': args.same_value_writes,
          'inputRoundtripEnabled': args.input_roundtrip, 'results': results, 'before': before_values, 'after': after_values, 'available': sum(s['available'] for s in before['features'].values())}
args.report.parent.mkdir(parents=True, exist_ok=True)
args.report.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
print(json.dumps({k: report[k] for k in ['ok', 'elapsedSeconds', 'sameValueWritesEnabled', 'available']}))
