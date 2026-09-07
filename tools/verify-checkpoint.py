"""Hardware integration: real process termination, resume, mux recovery, corruption.

Uses only generated artifacts; never modifies the supplied original input.
"""
import argparse
import hashlib
import json
import pathlib
import re
import subprocess
import uuid
import os
import sys

p = argparse.ArgumentParser()
p.add_argument('--engine', default='build-overlap/Release/RTXVideoHDRConvert.exe')
p.add_argument('--input', default='artifacts/first-test-sdr-tagged.mp4')
p.add_argument('--adapter', default='1')
p.add_argument('--frames', default='240')
a = p.parse_args()
root = pathlib.Path('artifacts') / ('checkpoint-test-' + uuid.uuid4().hex[:8])
root.mkdir()
engine = str(pathlib.Path(a.engine).resolve())
source = str(pathlib.Path(a.input).resolve())

def run(args, stop=None, conflict=None, hook=None):
    proc = subprocess.Popen([engine, *args], stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            text=True, encoding='utf-8', errors='replace')
    lines, job = [], None
    for line in proc.stdout:
        lines.append(line)
        if line.startswith('Logs: '):
            job = pathlib.Path(line[6:].strip()) / 'checkpoint.txt'
        if conflict and 'RTXHDR_STAGE mux_' in line:
            conflict.write_bytes(b'test-owned output conflict')
        if hook:
            hook(line, job)
        if stop and stop in line:
            proc.kill()
            stop = None
    code = proc.wait(timeout=60)
    text = ''.join(lines)
    (root / ('attempt-' + uuid.uuid4().hex[:8] + '.log')).write_text(text, encoding='utf-8')
    return code, text, job

def fresh(name, **kwargs):
    output = (root / name).resolve()
    args = [source, '--output', str(output), '--adapter', a.adapter,
            '--checkpoint-seconds', '2', '--max-frames', a.frames]
    return output, run(args, **kwargs)

def checksum(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

baseline, (code, text, baselinejob) = fresh('baseline.mkv')
assert code == 0, text
resumed, (code, text, job) = fresh('resumed.mkv', stop='RTXHDR_CHECKPOINT ')
assert code != 0 and job.exists(), text
saved = {f: checksum(f) for f in job.parent.rglob('video.mkv') if f.stat().st_size > 1000}
code, text, _ = run(['--resume', str(job), '--keep-intermediates'])
assert code == 0, text
assert re.search(r'\b(6[1-9]|[7-9]\d|\d{3,}) frames', text), text
for f, digest in saved.items():
    assert checksum(f) == digest, 'Saved segment was modified'

muxed, (code, text, muxjob) = fresh('mux-resume.mp4', stop='RTXHDR_STAGE mux_aac')
assert code != 0 and list(muxjob.parent.rglob('video.mkv')), text
code, text, _ = run(['--resume', str(muxjob)])
assert code == 0 and not re.search(r'\d+ frames', text), text

conflict = (root / 'publish-resume.mkv').resolve()
_, (code, text, publishjob) = fresh('publish-resume.mkv', conflict=conflict)
assert code != 0 and conflict.read_bytes() == b'test-owned output conflict' and list(publishjob.parent.rglob('video.mkv')), text
errors = list(publishjob.parent.rglob('error.json'))
assert errors
error = json.loads(errors[-1].read_text(encoding='utf-8-sig'))
assert error['stage'] == 'finalize' and error['saved_frame_count'] == int(a.frames), error
conflict.unlink()  # Only the exact test-owned conflict file created above.
code, text, _ = run(['--resume', str(publishjob)])
assert code == 0 and not re.search(r'\d+ frames', text), text
code, text, _ = run(['--resume', str(publishjob)])
assert code == 0, 'Recovery after publish / before success acknowledgement: ' + text

# Corrupt a copy of an unfinished job: final output does not yet exist.
import shutil
_, (code, text, corruptjob) = fresh('corrupt-input.mkv', stop='RTXHDR_CHECKPOINT ')
assert code != 0 and list(corruptjob.parent.rglob('video.mkv'))
damaged = root / 'damaged-job'
shutil.copytree(corruptjob.parent, damaged)
part = next(damaged.rglob('video.mkv'))
with part.open('r+b') as f:
    f.seek(100);f.write(b'corrupt-checkpoint-test')
code, text, _ = run(['--resume', str(damaged / 'checkpoint.txt')])
assert code != 0 and 'damaged' in text, text

# Completed output recovery must work after previous cleanup removed all segments.
code, text, _ = run(['--resume', str(job)])
assert code == 0 and 'RTXHDR_STAGE cleanup' in text, text
code, text, _ = run(['--resume', str(job)])
assert code == 0 and not re.search(r'\d+ frames', text), text

def assert_clean(checkpoint):
    for name in ('video.mkv', 'completed.mp4', 'completed.mkv'):
        assert not list(checkpoint.parent.rglob(name)), (checkpoint, name)
    assert list(checkpoint.parent.rglob('native.log')) and checkpoint.exists()
    reports = [json.loads(f.read_text(encoding='utf-8-sig')) for f in checkpoint.parent.rglob('cleanup.json')]
    assert reports and all(r['status']=='completed' for r in reports), reports
for checkpoint in (baselinejob, job, muxjob, publishjob):
    assert_clean(checkpoint)

# A second owner cannot open this job while its lock is held.
import ctypes
from ctypes import wintypes
kernel = ctypes.WinDLL('kernel32', use_last_error=True)
kernel.CreateFileW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD, ctypes.c_void_p, wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE]
kernel.CreateFileW.restype = wintypes.HANDLE
kernel.CloseHandle.argtypes = [wintypes.HANDLE]
lock = kernel.CreateFileW(str(job.parent / 'job.lock'), 0xC0000000, 0, None, 3, 128, None)
assert lock != wintypes.HANDLE(-1).value
try:
    code, text, _ = run(['--resume', str(job)])
    assert code != 0 and 'already running' in text, text
finally:
    kernel.CloseHandle(lock)

# Deny checkpoint replacement after initialization. A completed but uncommitted
# segment must never be reported as safely saved (same path used for disk errors).
held = []
def block_commit(line, checkpoint):
    if not held and re.search(r'\d+ frames', line):
        handle = kernel.CreateFileW(str(checkpoint), 0x80000000, 1, None, 3, 128, None)
        assert handle != wintypes.HANDLE(-1).value
        held.append(handle)
try:
    _, (code, text, blockedjob) = fresh('checkpoint-write-failure.mkv', hook=block_commit)
    assert code != 0 and 'Cannot commit checkpoint' in text, text
    report = json.loads(next(blockedjob.parent.rglob('error.json')).read_text(encoding='utf-8-sig'))
    committed = sum(int(line.rsplit(' ', 1)[1]) for line in blockedjob.read_text(encoding='utf-8').splitlines()[6:])
    assert report['saved_frame_count'] == committed < report['last_submitted_frame_count'], report
finally:
    for handle in held:
        kernel.CloseHandle(handle)
code, text, _ = run(['--resume', str(blockedjob)])
assert code == 0, text

# Mutate only a test-owned copy of the input; original input is never touched.
owned_input = root / 'input-copy.mp4'
shutil.copyfile(source, owned_input)
code, text, changedjob = run([str(owned_input.resolve()), '--output', str((root / 'changed-input.mkv').resolve()),
                             '--adapter', a.adapter, '--checkpoint-seconds', '2', '--max-frames', a.frames], stop='RTXHDR_CHECKPOINT ')
assert code != 0 and changedjob.exists()
stat = owned_input.stat();os.utime(owned_input, ns=(stat.st_atime_ns, stat.st_mtime_ns+1000000000))
code, text, _ = run(['--resume', str(changedjob)])
assert code != 0 and 'Original file changed' in text, text

# A locked intermediate must not turn an already published result into failure.
cleanup_handles=[]
def lock_cleanup(line, checkpoint):
    if not cleanup_handles and 'RTXHDR_STAGE mux_' in line:
        candidate=next(checkpoint.parent.rglob('video.mkv'))
        handle=kernel.CreateFileW(str(candidate),0x80000000,1,None,3,128,None)
        assert handle != wintypes.HANDLE(-1).value
        cleanup_handles.append(handle)
try:
    locked_output,(code,text,cleanupjob)=fresh('cleanup-warning.mkv',hook=lock_cleanup)
    assert code == 0 and locked_output.exists() and 'RTXHDR_CLEANUP_WARNING' in text, text
    assert list(cleanupjob.parent.rglob('video.mkv'))
finally:
    for handle in cleanup_handles:kernel.CloseHandle(handle)
code,text,_=run(['--resume',str(cleanupjob)])
assert code == 0 and not list(cleanupjob.parent.rglob('video.mkv')), text

# Verify frame count, decoded pixels, timestamps, HDR tags, and audio packets.
subprocess.run([sys.executable, 'tools/verify-overlap.py', str(baseline), str(resumed), '--frames', a.frames], check=True)
# Manifest corruption must fail before the engine touches segment data.
manifest = damaged / 'checkpoint.txt'
data = manifest.read_bytes();manifest.write_bytes(data[:-2] + b'X\n')
code, text, _ = run(['--resume', str(manifest)])
assert code != 0 and 'manifest is damaged' in text, text

print(json.dumps({'status': 'passed' , 'directory': str(root.resolve()),
                  'baseline': str(baseline), 'resumed': str(resumed),
                  'mux_resumed': str(muxed), 'publish_resumed': str(conflict)}, indent=2))
