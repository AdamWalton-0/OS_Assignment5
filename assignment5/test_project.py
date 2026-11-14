#!/usr/bin/env python3
"""Integration tests in a temporary directory; project data is not changed."""
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent
BINARY = ROOT / 'file_manager'
INITIAL_S = 'S101,Ana,CSC3320\nS102,Ben,CSC3320\n'
INITIAL_G = 'S101,Assignment4,88\nS102,Assignment4,91\n'
FINAL_S = 'S101,Ana,CSC3320\nS102,Benjamin,CSC3320\n'
FINAL_G = 'S101,Assignment4,88\nS102,Assignment4,94\nS101,Assignment5,95\n'

def run(base, *args, success=True):
    p = subprocess.run([str(base/'file_manager'), *args], cwd=base,
                       capture_output=True, text=True, timeout=10)
    assert (p.returncode == 0) == success, (args, p.stdout, p.stderr)
    return p

with tempfile.TemporaryDirectory() as tmp:
    base = Path(tmp)
    shutil.copy2(BINARY, base/'file_manager')
    for mode in ('deadlock', 'prevention', 'recovery'):
        run(base, mode, '--reset')
        d = base/'data'/mode
        log = (d/'execution.log').read_text()
        expected_s, expected_g = (INITIAL_S, INITIAL_G) if mode == 'deadlock' else (FINAL_S, FINAL_G)
        assert (d/'students.txt').read_text() == expected_s
        assert (d/'grades.txt').read_text() == expected_g
        assert (d/'backup.txt').read_text() == 'BACKUP - STUDENT RECORDS\n'+expected_s+'BACKUP - GRADES\n'+expected_g
        for i in range(1, 7):
            assert f'| T{i} | THREAD_START' in log
            assert f'| T{i} | THREAD_END' in log
        assert 'all six workers joined; errors=0' in log
        assert 'Total students = 2' in log
        if mode == 'deadlock':
            assert 'T1 -> T2 -> T1' in log and log.count('SAFE_ABORT') == 2
            assert (d/'audit.txt').read_text() == '2026-11-04,Project initialized\n'
        else:
            assert 'count=3 average=92.33 minimum=88 maximum=95' in log
            audit = (d/'audit.txt').read_text()
            assert 'Grade added for S101' in audit and 'Student and grade updated for S102' in audit
        if mode == 'prevention':
            assert 'DEADLOCK_DETECTED' not in log
        if mode == 'recovery':
            assert 'DEADLOCK_DETECTED' in log and 'RETRY_SUCCESS' in log
        print(f'PASS: {mode}: six threads, final files, locks, audit, backup, log')
    d = base/'data'/'prevention'
    before = {p.name: p.read_bytes() for p in d.glob('*.txt')}
    p = run(base, 'read', 'missing.txt', success=False)
    assert 'FILE_ERROR' in p.stdout
    run(base, 'read', '../students.txt', success=False)
    run(base, 'invalid-mode', success=False)
    assert before == {p.name:p.read_bytes() for p in d.glob('*.txt')}
    print('PASS: missing file, invalid path, invalid mode: errors; TXT files unchanged')
    run(base, 'recovery')
    assert (base/'data'/'recovery'/'grades.txt').read_text() == FINAL_G
    print('PASS: repeat run does not duplicate the grade')
    for _ in range(20):
        for mode in ('deadlock','prevention','recovery'):
            run(base, mode, '--reset')
            g = (base/'data'/mode/'grades.txt').read_text()
            assert g == (INITIAL_G if mode=='deadlock' else FINAL_G)
    print('PASS: 60 repeated mode runs: no hangs or inconsistent grades')
    # Directory and file links must not escape the project directory.
    (d/'grades.txt').unlink()
    (d/'grades.txt').symlink_to(base/'outside.txt')
    run(base, 'prevention', '--reset', success=False)
    assert not (base/'outside.txt').exists()
    print('PASS: symbolic link rejected without writing outside project data')
print('ALL TESTS PASSED')
