"""Read-only CLI contract checks. No request in this script can pass a write precondition."""
import argparse
import json
import pathlib
import subprocess
import re

p = argparse.ArgumentParser()
p.add_argument('executable', type=pathlib.Path)
p.add_argument('--report', required=True, type=pathlib.Path)
args = p.parse_args()
records = []

def check(command, ok=None):
    run = subprocess.run([str(args.executable.resolve()), *command], capture_output=True, timeout=30)
    value = json.loads(run.stdout.decode('utf-8'))
    assert value['schema'] == 1, (command, value)
    assert isinstance(value['ok'], bool), (command, value)
    if ok is not None:
        assert value['ok'] is ok, (command, value)
    assert run.returncode == (0 if value['ok'] else 1), (command, run.returncode, value)
    records.append({'command': command, 'exitCode': run.returncode, 'result': value})
    return value

check(['--help'], True)
expected_version = re.search(r'project\(ThinkVantage VERSION ([0-9.]+)', (pathlib.Path(__file__).resolve().parents[1] / 'CMakeLists.txt').read_text(encoding='utf-8')).group(1)
assert check(['--version'], True)['version'] == expected_version
for command in [
    ['--not-a-command'], ['--probe', 'extra'], ['--apply'], ['--apply', 'volume'],
    ['--apply', 'volume', 'nan'], ['--apply', 'volume', '-1'],
    ['--apply', 'ioctl', '2'], ['--apply-if', 'power', '1'],
    ['--apply-if', 'power', '1', '-999', 'unknown'],
    ['--get'], ['--gpu-processes', 'extra'], ['--apply-if', 'power', '1', '1.5'],
    ['--apply-target', 'volume', '50'],
    ['--apply-target', 'volume', '50', 'invalid', 'request-test', 'bad-generation'],
]:
    check(command, False)
snapshot = check(['--probe'], True)
check(['--get', 'not-a-feature'], False)
check(['--list'], True)
gpu = check(['--gpu-processes'])
assert isinstance(gpu['available'], bool) and isinstance(gpu['processes'], list), gpu
assert gpu['ok'] == gpu['available'], gpu
power = snapshot['features']['power']
volume = snapshot['features']['volume']
if volume['available']:
    result = check(['--apply-target', 'volume', str(volume['value']), 'invalid-endpoint', 'request-test', '1'], False)
    assert result['errorCode'] == 'target_changed' and result['conflict'] is True, result
if power['available']:
    # Power modes are in [0,2], so -999 cannot match. Backend must reject while
    # holding the write lock, before reaching any setter.
    result = check(['--apply-if', 'power', str(power['value']), '-999'], False)
    assert result['conflict'] is True and 'unknownOutcome' not in result, result
args.report.parent.mkdir(parents=True, exist_ok=True)
args.report.write_text(json.dumps({'ok': True, 'checks': len(records), 'records': records}, ensure_ascii=False, indent=2), encoding='utf-8')
print(json.dumps({'ok': True, 'checks': len(records)}))
