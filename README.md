# Business Maintenance Tool (C++)

A self-contained Windows maintenance utility in C++ (no external libraries required) providing:

- System actions: shutdown, restart, logoff, update-and-restart
- File/folder backup: multiple sources to multiple destinations
- Saved backup job configurations (`config.txt`) with persistent settings
- Override (replace prior backup folder) or timestamped versioned backups

## Build (No Dependencies)
Requires Microsoft Visual C++ Build Tools (present on most dev setups) or full Visual Studio. On a clean Windows install with only the Build Tools installed this works without extra packages.

### Option 1: Using build script (recommended)
```powershell
.\build.bat
```
The script auto-detects Visual Studio installation.

### Option 2: From Developer Command Prompt
```powershell
cl /std:c++17 /O2 /EHsc maintenance_tool.cpp /Fe:maintenance_tool.exe
```

### Build GUI version
```powershell
.\build_gui.bat
```
Produces `maintenance_tool_gui.exe` with Win32 interface.

## Quick Start Test
After building, test the tool:
```powershell
.\maintenance_tool.exe --help
.\maintenance_tool.exe --self-test
```

## Usage

## Interactive Job Creation
```powershell
./maintenance_tool.exe --add-job
```
Prompts:
- Job name
- Source paths (files or folders, enter blank line to finish)
- Destination paths (folders; auto-created)
- Override old backups (y/N)

## Run a Backup
Standard (full copy or incremental if enabled in job):
```powershell
./maintenance_tool.exe --backup JOB_NAME
```
Dry run (list planned copies only):
```powershell
./maintenance_tool.exe --backup JOB_NAME --dry-run
```
Zip the created backup folder (override or latest timestamped):
```powershell
./maintenance_tool.exe --backup JOB_NAME --zip
```

Show progress (size-based):
```powershell
./maintenance_tool.exe --backup JOB_NAME --progress
```

## List Jobs
```powershell
./maintenance_tool.exe --list-jobs
```

## List Backups (Existing Versions)
```powershell
./maintenance_tool.exe --list-backups JOB_NAME
```

## Restore Latest or Specific Version
Latest:
```powershell
./maintenance_tool.exe --restore JOB_NAME
```
Specific timestamp:
```powershell
./maintenance_tool.exe --restore JOB_NAME --version YYYYMMDD_HHMMSS
```
Specify destination base if multiple destinations:
```powershell
./maintenance_tool.exe --restore JOB_NAME --source-dest D:/backups
```

## Launch Program Shortcuts
Launch all configured programs for a job:
```powershell
./maintenance_tool.exe --launch JOB_NAME
```
Useful for opening related tools after restore or as part of workflow automation.

## Verify Backup Integrity
Latest:
```powershell
./maintenance_tool.exe --verify JOB_NAME
```
Specific version:
```powershell
./maintenance_tool.exe --verify JOB_NAME --version YYYYMMDD_HHMMSS
```
Reports counts of ok / missing / mismatch and logs result.

## System Operations (Require Confirmation)
```powershell
./maintenance_tool.exe --shutdown
./maintenance_tool.exe --restart
./maintenance_tool.exe --logoff
./maintenance_tool.exe --update-restart
```
Each prompts for confirmation unless `--force` is added.

## Config File Format (`config.txt`)
Plain-text custom format:
```
[job:Example]
sources=C:/data/file.txt|C:/projects/myapp
destinations=D:/backups|E:/offsite
override=0
incremental=1
shortcuts=C:/Program Files/MyApp/app.exe|C:/Tools/utility.exe
schedule_enabled=1
schedule_interval=daily
schedule_value=1
schedule_time=02:00
last_run=0
```
Multiple jobs appended sequentially. Separator for multiple paths: `|`.

See `config.example.txt` for a complete example with multiple job types.

## Override & Incremental Behavior
- Override = 1: destination uses folder `<dest>/JOB_NAME` (emptied each run).
- Override = 0: creates `<dest>/JOB_NAME_YYYYMMDD_HHMMSS` per run.
- Incremental = 1 (and not override): skips unchanged files by comparing size & modified time against previous run's `manifest.txt`.

## Manifest Format
Each line: `relativePath|size|mtime|sha256hex` (older backups may lack hash; verify still works by size & mtime).

## Update & Restart Notes
Attempts sequence via `UsoClient` commands; requires administrative privileges. If unavailable, warnings logged.

## Self-Test
Creates temporary source + file, performs backup, validates copy and logs results.
```powershell
./maintenance_tool.exe --self-test
```

## Logging
All actions appended to `maintenance.log` in the executable directory:
```
YYYY-MM-DD HH:MM:SS | action | detail | status
```
Examples:
```
2025-11-30 10:12:03 | backup | JOB dest=D:/backups | success
2025-11-30 10:13:15 | restore-item | C:/data/file.txt | ok
```

## Automatic Scheduled Backups
Jobs can have automatic schedules configured during creation or in `config.txt`:

### Schedule Configuration Fields
- `schedule_enabled=1` - Enable scheduling (0 to disable)
- `schedule_interval` - Type: `mins`, `hours`, `daily`, `weekly`, `monthly`, `once`
- `schedule_value` - Number of intervals (e.g., 2 for every 2 hours)
- `schedule_time` - Time to run in HH:MM format (e.g., 02:00 for 2 AM)
- `schedule_dest` - Optional: specific destination for scheduled backups (overrides job destinations)
- `schedule_onetime=1` - One-time schedule (disables after first run)
- `additional_schedules` - Multiple schedules in format: `interval|value|time|dest|onetime|lastrun`
- `last_run` - Timestamp of last execution (auto-updated)

### Running the Scheduler
**Manual check:**
```powershell
./maintenance_tool.exe --run-scheduler
```

**Daemon mode (continuous):**
```powershell
./maintenance_tool.exe --daemon 60
```
Checks every 60 minutes. Runs in foreground - use Task Scheduler for true background operation.

### Windows Task Scheduler Integration
1. Create task: Run `start_scheduler.bat` on system startup
2. Or use Task Scheduler GUI:
   - Action: Start Program = `C:\path\to\maintenance_tool.exe`
   - Arguments: `--daemon 60`
   - Trigger: At system startup
   - Run whether user is logged in or not

### Schedule Types
- **mins/hours**: Runs every N minutes/hours from last run
- **daily/weekly/monthly**: Runs at specified time (e.g., daily at 02:00)
- **once**: Runs once when time arrives, then stops

### Multiple Schedules per Job
When creating a job, you can add multiple schedules (e.g., hourly to local drive + daily to network):
```
Primary schedule: daily at 02:00 -> D:/DailyBackup
Additional schedule 1: weekly at 23:00 -> E:/WeeklyBackup
Additional schedule 2: monthly at 01:00 -> F:/MonthlyArchive
```
Each schedule can have its own destination, interval, time, and one-time setting.

## Zip Compression
`--zip` uses PowerShell `Compress-Archive` (built-in). Produces `<backupfolder>.zip`. Failures recorded in log.

## Restore Safety
Restore requires typing `YES` unless `--force` used. Existing files are overwritten. Log captures each restored item and status.

## GUI (Experimental)
Optional `maintenance_gui.cpp` provides minimal Win32 interface (job selection, Backup, Restore Latest). Build:
```powershell
cl /std:c++17 /O2 /EHsc maintenance_tool.cpp maintenance_gui.cpp /Fe:maintenance_tool_gui.exe user32.lib gdi32.lib comdlg32.lib
```
Run:
```powershell
./maintenance_tool_gui.exe
```

## Future Enhancements
- Optional built-in ZIP (remove PowerShell dependency)
- Hash-based integrity & deduplication
- Event Log integration
- GUI polish (job editing, progress bars)
- Encryption (DPAPI) for sensitive archives
 - Native ZIP writer / encrypted archive

## Installation on Fresh Windows
1. Download and install Visual Studio Build Tools (free): https://visualstudio.microsoft.com/downloads/
2. Select "Desktop development with C++" workload during installation
3. Run `build.bat` to compile
4. Copy `maintenance_tool.exe` to desired location (runs standalone)
5. Create jobs with `--add-job` or copy `config.example.txt` to `config.txt`

## Disclaimer
Verify backups and restorations regularly. Some system actions need admin elevation. Always test restore procedures before relying on backups in production.
