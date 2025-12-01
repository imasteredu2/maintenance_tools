import os
import sys
import json
import shutil
import argparse
import datetime
import subprocess
from typing import List, Dict, Any

CONFIG_FILE = os.path.join(os.path.dirname(__file__), 'config.json')


def load_config() -> Dict[str, Any]:
    if not os.path.exists(CONFIG_FILE):
        return {"jobs": {}}
    try:
        with open(CONFIG_FILE, 'r', encoding='utf-8') as f:
            return json.load(f)
    except Exception:
        return {"jobs": {}}


def save_config(cfg: Dict[str, Any]) -> None:
    with open(CONFIG_FILE, 'w', encoding='utf-8') as f:
        json.dump(cfg, f, indent=2)


def ensure_paths(paths: List[str]) -> List[str]:
    normalized = []
    for p in paths:
        p = p.strip().strip('"')
        if p:
            normalized.append(os.path.abspath(p))
    return normalized


def add_job_interactive():
    cfg = load_config()
    jobs = cfg.setdefault('jobs', {})
    name = input('Enter job name: ').strip()
    if not name:
        print('Job name required.')
        return
    if name in jobs:
        print('Job already exists.')
        return
    print('Enter source paths (files/folders). Blank line to finish:')
    sources = []
    while True:
        line = input('Source> ').strip()
        if not line:
            break
        sources.append(line)
    print('Enter destination folder paths. Blank line to finish:')
    destinations = []
    while True:
        line = input('Destination> ').strip()
        if not line:
            break
        destinations.append(line)
    override_in = input('Override previous backups? (y/N): ').strip().lower()
    override = override_in == 'y'
    job = {
        'sources': ensure_paths(sources),
        'destinations': ensure_paths(destinations),
        'override': override
    }
    jobs[name] = job
    save_config(cfg)
    print(f'Job {name} saved.')


def list_jobs():
    cfg = load_config()
    jobs = cfg.get('jobs', {})
    if not jobs:
        print('No jobs defined.')
        return
    for name, job in jobs.items():
        print(f'- {name}:')
        print(f'    Sources ({len(job.get("sources", []))}):')
        for s in job.get('sources', []):
            print(f'      {s}')
        print(f'    Destinations ({len(job.get("destinations", []))}):')
        for d in job.get('destinations', []):
            print(f'      {d}')
        print(f'    Override: {job.get("override")}')


def _copy_source_to_dest(src: str, dest: str):
    if os.path.isfile(src):
        dest_parent = os.path.dirname(dest)
        os.makedirs(dest_parent, exist_ok=True)
        shutil.copy2(src, dest)
    elif os.path.isdir(src):
        if os.path.exists(dest):
            # Merge copy: copy contents recursively
            for root, dirs, files in os.walk(src):
                rel = os.path.relpath(root, src)
                target_root = os.path.join(dest, rel) if rel != '.' else dest
                os.makedirs(target_root, exist_ok=True)
                for f in files:
                    shutil.copy2(os.path.join(root, f), os.path.join(target_root, f))
        else:
            shutil.copytree(src, dest)
    else:
        print(f'WARN: Source missing {src}')


def perform_backup(job_name: str, dry_run: bool = False):
    cfg = load_config()
    job = cfg.get('jobs', {}).get(job_name)
    if not job:
        print(f'Job {job_name} not found.')
        return
    sources = job.get('sources', [])
    destinations = job.get('destinations', [])
    override = job.get('override', False)
    if not sources or not destinations:
        print('Job has empty sources or destinations.')
        return
    timestamp = datetime.datetime.now().strftime('%Y%m%d_%H%M%S')
    for dest_base in destinations:
        dest_base = os.path.abspath(dest_base)
        os.makedirs(dest_base, exist_ok=True)
        if override:
            # Use fixed folder named after job
            target_root = os.path.join(dest_base, job_name)
            if os.path.exists(target_root):
                # Clear existing contents
                for item in os.listdir(target_root):
                    item_path = os.path.join(target_root, item)
                    if os.path.isfile(item_path) or os.path.islink(item_path):
                        os.unlink(item_path)
                    else:
                        shutil.rmtree(item_path)
        else:
            target_root = os.path.join(dest_base, f'{job_name}_{timestamp}')
        if dry_run:
            print(f'[DRY] Destination root: {target_root}')
        else:
            os.makedirs(target_root, exist_ok=True)
        for src in sources:
            if not os.path.exists(src):
                print(f'WARN missing: {src}')
                continue
            rel_name = os.path.basename(src.rstrip('/\\'))
            dest_path = os.path.join(target_root, rel_name)
            if dry_run:
                print(f'[DRY] Copy {src} -> {dest_path}')
            else:
                _copy_source_to_dest(src, dest_path)
        print(f'Backup to {dest_base} completed (override={override}, dry={dry_run}).')


def system_shutdown():
    subprocess.run(['shutdown', '/s', '/t', '0'])


def system_restart():
    subprocess.run(['shutdown', '/r', '/t', '0'])


def system_logoff():
    subprocess.run(['shutdown', '/l'])


def system_update_and_restart():
    # Attempts Windows Update orchestration; requires admin; may not succeed silently.
    cmds = [
        'UsoClient StartScan',
        'UsoClient StartDownload',
        'UsoClient StartInstall',
        'UsoClient RestartDevice'
    ]
    ps = '; '.join(cmds)
    try:
        completed = subprocess.run(['powershell', '-Command', ps], capture_output=True, text=True)
        print(completed.stdout)
        if completed.stderr:
            print('Errors:', completed.stderr)
    except Exception as e:
        print(f'Update command failed: {e}')


def self_test():
    # Create a temporary test job in memory (not persisted) to validate backup logic
    import tempfile
    tmp_src_dir = tempfile.mkdtemp(prefix='mnt_src_')
    file_path = os.path.join(tmp_src_dir, 'sample.txt')
    with open(file_path, 'w', encoding='utf-8') as f:
        f.write('sample data')
    tmp_dest_dir = tempfile.mkdtemp(prefix='mnt_dest_')
    test_job = {
        'sources': [tmp_src_dir, file_path],
        'destinations': [tmp_dest_dir],
        'override': False
    }
    # Simulate copy
    sources = test_job['sources']
    destinations = test_job['destinations']
    timestamp = datetime.datetime.now().strftime('%Y%m%d_%H%M%S')
    target_root = os.path.join(tmp_dest_dir, f'TEST_{timestamp}')
    os.makedirs(target_root, exist_ok=True)
    for src in sources:
        rel_name = os.path.basename(src.rstrip('/\\'))
        dest_path = os.path.join(target_root, rel_name)
        _copy_source_to_dest(src, dest_path)
    # Verify
    ok = os.path.exists(os.path.join(target_root, os.path.basename(file_path)))
    print('Self-test backup OK' if ok else 'Self-test backup FAILED')


def parse_args(argv: List[str]):
    ap = argparse.ArgumentParser(description='Business Maintenance Tool')
    ap.add_argument('--add-job', action='store_true', help='Interactively add a backup job')
    ap.add_argument('--list-jobs', action='store_true', help='List all backup jobs')
    ap.add_argument('--backup', metavar='JOB', help='Run backup for JOB')
    ap.add_argument('--dry-run', action='store_true', help='Simulate backup copy operations')
    ap.add_argument('--shutdown', action='store_true', help='Shutdown the computer')
    ap.add_argument('--restart', action='store_true', help='Restart the computer')
    ap.add_argument('--logoff', action='store_true', help='Log off current user')
    ap.add_argument('--update-restart', action='store_true', help='Trigger Windows Update then restart')
    ap.add_argument('--self-test', action='store_true', help='Run internal self-test')
    return ap.parse_args(argv)


def main():
    args = parse_args(sys.argv[1:])
    acted = False
    if args.add_job:
        add_job_interactive(); acted = True
    if args.list_jobs:
        list_jobs(); acted = True
    if args.backup:
        perform_backup(args.backup, dry_run=args.dry_run); acted = True
    if args.shutdown:
        system_shutdown(); acted = True
    if args.restart:
        system_restart(); acted = True
    if args.logoff:
        system_logoff(); acted = True
    if args.update_restart:
        system_update_and_restart(); acted = True
    if args.self_test:
        self_test(); acted = True
    if not acted:
        # Show help if no action
        parse_args(['-h'])


if __name__ == '__main__':
    main()
