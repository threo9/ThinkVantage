"""Native Windows lifecycle checks. No hardware settings are changed."""
import argparse
import ctypes as c
from ctypes import wintypes as w
import json
import pathlib
import subprocess
import time

parser = argparse.ArgumentParser()
parser.add_argument('executable', type=pathlib.Path)
parser.add_argument('--report', type=pathlib.Path, required=True)
args = parser.parse_args()
exe = str(args.executable.resolve())
user = c.WinDLL('user32', use_last_error=True)
callback_type = c.WINFUNCTYPE(w.BOOL, w.HWND, w.LPARAM)
user.EnumWindows.argtypes = [callback_type, w.LPARAM]
user.GetWindowThreadProcessId.argtypes = [w.HWND, c.POINTER(w.DWORD)]
user.IsWindowVisible.argtypes = [w.HWND]
user.GetWindowTextW.argtypes = [w.HWND, w.LPWSTR, c.c_int]
user.PostMessageW.argtypes = [w.HWND, w.UINT, w.WPARAM, w.LPARAM]

def windows(pid):
    result = []
    @callback_type
    def callback(handle, _):
        owner = w.DWORD()
        user.GetWindowThreadProcessId(handle, c.byref(owner))
        if owner.value == pid and user.IsWindowVisible(handle):
            title = c.create_unicode_buffer(256)
            user.GetWindowTextW(handle, title, len(title))
            result.append((handle, title.value))
        return True
    user.EnumWindows(callback, 0)
    return result

def wait_for(condition, seconds=8):
    until = time.monotonic() + seconds
    while time.monotonic() < until:
        result = condition()
        if result:
            return result
        time.sleep(.1)
    raise AssertionError('Timed out waiting for native window transition')

def command(*arguments):
    result = subprocess.run([exe, *arguments], timeout=12)
    assert result.returncode == 0, (arguments, result.returncode)

report = {}
started = time.monotonic()
p = subprocess.Popen([exe, '--tray'])
try:
    time.sleep(2)
    assert p.poll() is None and not windows(p.pid), windows(p.pid)
    report['backgroundStartup'] = True
    command()  # A second launch opens the original process's window.
    main = wait_for(lambda: next((hwnd for hwnd, title in windows(p.pid) if title == 'ThinkVantage'), None))
    report['singleInstanceOpen'] = True
    user.PostMessageW(main, 0x0010, 0, 0)  # WM_CLOSE
    wait_for(lambda: not windows(p.pid))
    assert p.poll() is None
    report['closeToTray'] = True
    command('--quick')
    popup = wait_for(lambda: next((hwnd for hwnd, title in windows(p.pid) if '快捷面板' in title), None))
    report['quickPanelEntry'] = True
    user.PostMessageW(popup, 0x0100, 0x1B, 0)  # Escape dismisses the popup.
    user.PostMessageW(popup, 0x0101, 0x1B, 0)
    wait_for(lambda: not windows(p.pid))
    report['escapeDismissal'] = True
    command('--quit')
    assert p.wait(timeout=8) == 0
    report['explicitExit'] = True
    p = subprocess.Popen([exe, '--tray'])
    time.sleep(1)
    command('--quit')
    assert p.wait(timeout=8) == 0
    report['restartAfterExit'] = True
finally:
    if p.poll() is None:
        subprocess.run([exe, '--quit'], timeout=12)
        try:
            p.wait(timeout=8)
        except subprocess.TimeoutExpired:
            p.terminate()  # Only the process created by this check.
            p.wait(timeout=5)
report.update(ok=True, elapsedSeconds=round(time.monotonic()-started, 3))
args.report.parent.mkdir(parents=True, exist_ok=True)
args.report.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
print(json.dumps(report))
