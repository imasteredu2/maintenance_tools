# Quick Start Guide - Business Maintenance Tool

## First Time Setup

1. **Build the tool**
   ```powershell
   .\build.bat
   ```

2. **Verify installation**
   ```powershell
   .\maintenance_tool.exe --self-test
   ```

3. **Create your first backup job**
   ```powershell
   .\maintenance_tool.exe --add-job
   ```
   - Job name: `MyDocuments`
   - Source: `C:\Users\YourName\Documents`
   - Destination: `D:\Backups`
   - Override: `N` (creates timestamped versions)
   - Incremental: `Y` (skips unchanged files)
   - Shortcuts: `C:\Program Files\MyApp\app.exe` (optional, blank to skip)
   - Blank line to finish shortcuts
   - Scheduled backups: `Y`
   - Interval: `daily` (or mins/hours/weekly/monthly/once)
   - Interval value: `1`
   - Run time: `02:00` (2 AM)
   - Schedule destination: `D:\ScheduledBackups` (or blank to use job destinations)
   - One-time schedule: `N` (Y to disable after first run)
   - Additional schedules: `Y` to add more (e.g., weekly + monthly backups)
     - Each additional schedule can have its own interval, time, and destination

## Common Tasks

### Run a backup
```powershell
.\maintenance_tool.exe --backup MyDocuments
```

### Run backup with progress display
```powershell
.\maintenance_tool.exe --backup MyDocuments --progress
```

### Run backup and create ZIP archive
```powershell
.\maintenance_tool.exe --backup MyDocuments --zip
```

### List all backup versions
```powershell
.\maintenance_tool.exe --list-backups MyDocuments
```

### Verify backup integrity
```powershell
.\maintenance_tool.exe --verify MyDocuments
```

### Restore latest backup
```powershell
.\maintenance_tool.exe --restore MyDocuments
```
Type `YES` to confirm.

### Restore specific version
```powershell
.\maintenance_tool.exe --list-backups MyDocuments
# Note the timestamp (e.g., 20251130_143022)
.\maintenance_tool.exe --restore MyDocuments --version 20251130_143022
```

### Launch program shortcuts
```powershell
.\maintenance_tool.exe --launch MyDocuments
```
Opens all configured programs (useful after restore or as part of workflow).

### Check and run scheduled backups
```powershell
.\maintenance_tool.exe --run-scheduler
```
Runs any jobs with schedules that are currently due.

### Run scheduler as daemon (continuous)
```powershell
.\maintenance_tool.exe --daemon 30
```
Checks every 30 minutes for scheduled backups. Press Ctrl+C to stop.

## Scheduled Backups (Windows Task Scheduler)

### Method 1: Using built-in scheduler daemon
Create a batch file `start_scheduler.bat`:
```batch
@echo off
cd /d "C:\Path\To\MaintenanceTool"
maintenance_tool.exe --daemon 60
```
Add to Task Scheduler:
1. Open Task Scheduler
2. Create Basic Task → "Backup Scheduler"
3. Trigger: At startup
4. Action: Start a program → Browse to `start_scheduler.bat`
5. Finish

The daemon runs continuously, checking every 60 minutes for scheduled backups.

### Method 2: Task Scheduler calling scheduler directly
```batch
@echo off
cd /d "C:\Path\To\MaintenanceTool"
maintenance_tool.exe --run-scheduler
```
Add to Task Scheduler:
1. Open Task Scheduler
2. Create Basic Task
3. Set trigger (every hour, every 30 minutes, etc.)
4. Action: Start a program → Point to batch file
5. Finish

This approach runs the scheduler check on a fixed schedule rather than continuously.

### Method 3: Manual backup via Task Scheduler
```batch
@echo off
cd /d "C:\Path\To\MaintenanceTool"
maintenance_tool.exe --backup MyDocuments --zip
if %ERRORLEVEL% EQU 0 (
    echo Backup successful >> backup_history.log
) else (
    echo Backup failed >> backup_history.log
)
```

Add to Task Scheduler:
1. Open Task Scheduler
2. Create Basic Task
3. Set trigger (daily, weekly, etc.)
4. Action: Start a program → Browse to `scheduled_backup.bat`
5. Finish

## System Maintenance

### Shutdown computer
```powershell
.\maintenance_tool.exe --shutdown
```
Type `YES` to confirm, or add `--force` to skip confirmation.

### Restart computer
```powershell
.\maintenance_tool.exe --restart
```

### Run Windows Update and restart
```powershell
.\maintenance_tool.exe --update-restart
```
*Requires administrator privileges*

## Troubleshooting

### Check logs
All operations are logged to `maintenance.log`:
```powershell
type maintenance.log
```

### Verify backup succeeded
```powershell
.\maintenance_tool.exe --verify MyDocuments
```
Reports: entries, ok, missing, mismatch counts.

### Test restore to temporary location
1. Edit `config.txt` manually
2. Change job source paths to temporary test folder
3. Run restore
4. Verify files, then change paths back

## Advanced Usage

### Multiple destinations (redundancy)
When creating job, add multiple destinations:
- Destination 1: `D:\Backups`
- Destination 2: `E:\OffSite`
- Blank line to finish

Every destination gets a full copy.

### Override vs Timestamped
- **Override=0** (default): Creates `JobName_YYYYMMDD_HHMMSS` folders (versioned backups)
- **Override=1**: Uses fixed `JobName` folder (replaces previous backup, saves space)

### Incremental backups
- **Incremental=1**: Skips files unchanged since last backup (faster, smaller)
- **Incremental=0**: Copies all files every time (slower, but safer)

### Multiple schedules per job
When creating jobs, you can configure:
- **Primary schedule**: Main backup schedule (e.g., daily at 2 AM to local drive)
- **Additional schedules**: Extra schedules with different intervals/destinations
  - Example: Hourly to D:\, daily to E:\, weekly to F:\
  - Each schedule can have its own destination path
  - Mix one-time and recurring schedules in same job

### One-time schedules
Set `schedule_onetime=1` or answer `Y` when creating job:
- Runs once when scheduled time arrives
- Automatically disables after first run
- Useful for end-of-day reports, project deliverables, etc.

### Schedule-specific destinations
Each schedule can override job destinations:
- Primary schedule → `D:\HourlyBackup`
- Additional schedule 1 → `E:\DailyBackup`
- Additional schedule 2 → `F:\WeeklyArchive`
- Different backup retention policies per destination

### Dry run (test before running)
```powershell
.\maintenance_tool.exe --backup MyDocuments --dry-run
```
Shows what would be copied without actually copying.

## Best Practices

1. **Test restores regularly** - A backup you haven't tested is not a real backup
2. **Use multiple destinations** - Protects against drive failure
3. **Verify after backup** - Run `--verify` to ensure integrity
4. **Enable incremental** for daily backups - Saves time and space
5. **Keep logs** - Review `maintenance.log` periodically
6. **Schedule automatic backups** - Use Task Scheduler for hands-free operation

## GUI Mode (Optional)

Build and run the graphical interface:
```powershell
.\build_gui.bat
.\maintenance_tool_gui.exe
```

Features:
- Select job from list
- Click Backup or Restore buttons
- Status messages shown in window
- Simplified interface for non-technical users

---

For complete documentation, see `README.md`
