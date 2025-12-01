# Scheduling Features - Enhanced Implementation

## New Features Added

### 1. Schedule-Specific Destination Paths
Each schedule can now specify its own backup destination, overriding the job's default destinations.

**Configuration:**
```
schedule_dest=D:/ScheduledBackups/Hourly
```

**Benefits:**
- Different retention policies per schedule (hourly to fast drive, daily to archive)
- Separate local vs network backup locations
- Better organization of backup versions

### 2. One-Time Schedules
Schedules can be configured to run once and automatically disable.

**Configuration:**
```
schedule_interval=once
schedule_time=17:00
schedule_onetime=1
```

**Use Cases:**
- End-of-day report backups
- Project milestone archives
- One-time data exports
- Special event backups

### 3. Multiple Schedules Per Job
Jobs can now have unlimited additional schedules beyond the primary schedule.

**Configuration:**
```
additional_schedules=weekly|1|23:00|E:/WeeklyBackup|0|0|monthly|1|01:00|F:/MonthlyArchive|0|0
```

**Format:** `interval|value|time|destination|onetime|lastrun`

**Example Scenario:**
- **Primary:** Hourly incremental to D:\HourlyBackup
- **Schedule 1:** Daily full to E:\DailyArchive at 2 AM
- **Schedule 2:** Weekly to network F:\WeeklyBackup Sunday 11 PM
- **Schedule 3:** Monthly to offsite G:\MonthlyArchive 1st day 1 AM

### 4. "Once" Interval Type
New interval type for single-execution schedules.

**Behavior:**
- Runs when scheduled time arrives
- Never runs again after first execution
- Can be combined with `schedule_onetime=1` for auto-disable

## Configuration Fields Reference

### Primary Schedule Fields
```
schedule_enabled=1                     # 0 or 1
schedule_interval=daily                # mins, hours, daily, weekly, monthly, once
schedule_value=1                       # Number of intervals
schedule_time=02:00                    # HH:MM format
schedule_dest=D:/CustomDestination     # Optional override path
schedule_onetime=0                     # 0 or 1 - disable after first run
last_run=1733000000                    # Unix timestamp (auto-managed)
```

### Additional Schedules Field
```
additional_schedules=interval|value|time|dest|onetime|lastrun|interval2|value2|...
```

**Example:**
```
additional_schedules=hourly|2|00:00|D:/Hourly|0|0|daily|1|03:00|E:/Daily|0|0
```
Creates two additional schedules:
1. Every 2 hours → D:/Hourly
2. Daily at 3 AM → E:/Daily

## Interactive Job Creation Flow

When creating a job with `--add-job`, users are prompted:

1. Enable scheduled backups? (y/N)
2. Schedule interval (mins/hours/daily/weekly/monthly/once):
3. Interval value:
4. Time to run (HH:MM):
5. **NEW:** Schedule-specific destination (blank to use job destinations):
6. **NEW:** One-time schedule (disable after first run)? (y/N):
7. **NEW:** Add additional schedules for this job? (y/N):
   - If yes, loop for each additional schedule:
     - Interval:
     - Interval value:
     - Time (HH:MM):
     - Destination path:
     - One-time? (y/N):
     - Add another schedule? (y/N):

## Scheduler Behavior

### Primary Schedule
- Checked by `shouldRunScheduled(job)`
- Uses `job.scheduleDest` if set, otherwise `job.destinations`
- Updates `job.lastRun` after execution
- Disables `job.scheduleEnabled` if `job.scheduleOneTime` is true

### Additional Schedules
- Processed in `runScheduler()` loop
- Each schedule tracked independently with own `lastRun` timestamp
- Can have different destinations per schedule
- One-time schedules marked as "disabled" after first run

### Execution Logic
```cpp
void runScheduler() {
    // For each job:
    //   1. Check primary schedule (if enabled)
    //   2. Override destinations if schedule_dest is set
    //   3. Run backup
    //   4. Restore original destinations
    //   5. Update lastRun
    //   6. Disable if one-time
    //
    //   7. Loop through additional_schedules
    //   8. For each additional schedule:
    //      - Check if due
    //      - Override destinations if schedule has dest
    //      - Run backup
    //      - Update schedule's lastRun
    //      - Mark as "disabled" if one-time
}
```

## Example Configurations

### Hourly + Daily + Weekly Backup
```
[job:CriticalData]
sources=C:/Data/Critical
destinations=D:/Backups/Default
schedule_enabled=1
schedule_interval=hours
schedule_value=2
schedule_time=00:00
schedule_dest=D:/Backups/Hourly
schedule_onetime=0
additional_schedules=daily|1|02:00|E:/Backups/Daily|0|0|weekly|1|23:00|F:/Backups/Weekly|0|0
```

Result:
- Every 2 hours → D:/Backups/Hourly
- Daily at 2 AM → E:/Backups/Daily
- Weekly Sunday 11 PM → F:/Backups/Weekly

### One-Time Event Backup
```
[job:ProjectDeliverable]
sources=C:/Projects/ClientX/Final
destinations=D:/Backups/Projects
schedule_enabled=1
schedule_interval=once
schedule_value=1
schedule_time=17:00
schedule_dest=D:/Deliverables/ClientX_2025-11-30
schedule_onetime=1
```

Result:
- Runs once at 5 PM today
- Backs up to specific deliverable folder
- Auto-disables after execution

### Mixed Intervals with Different Destinations
```
[job:Development]
sources=C:/Dev/Project
destinations=D:/Backups/Dev
schedule_enabled=1
schedule_interval=mins
schedule_value=30
schedule_time=00:00
schedule_dest=D:/Dev/Continuous
additional_schedules=daily|1|01:00|E:/Dev/Daily|0|0|monthly|1|01:00|F:/Dev/Archive|1|0
```

Result:
- Every 30 minutes → D:/Dev/Continuous (continuous work backup)
- Daily at 1 AM → E:/Dev/Daily (daily snapshots)
- Monthly at 1 AM → F:/Dev/Archive (one-time monthly archive, disables after first run)

## Listing Jobs with Schedules

The `--list-jobs` command now displays all schedule information:

```
- DailyDocuments
    Sources (2):
      C:/Users/YourName/Documents
      C:/Users/YourName/Desktop
    Destinations (2):
      D:/Backups/Daily
      E:/OffSiteBackup
    Override: false
    Incremental: true
    Shortcuts (2):
      C:/Program Files/Notepad++/notepad++.exe
      C:/Windows/System32/calc.exe
    Schedule: enabled - every 1 daily at 02:00 -> D:/ScheduledBackups/Documents (last: 2025-11-30 02:00:15)
    Additional Schedules (2):
      - every 1 weekly at 23:00 -> E:/WeeklyBackup (last: 2025-11-26 23:00:08)
      - every 1 monthly at 01:00 -> F:/MonthlyArchive (one-time)
```

## Commands Reference

### Run Scheduler Once
```powershell
maintenance_tool.exe --run-scheduler
```
Checks all jobs, runs due backups, updates timestamps.

### Run Scheduler as Daemon
```powershell
maintenance_tool.exe --daemon 60
```
Continuous operation, checks every 60 minutes.

### Manual Backup (Ignores Schedule)
```powershell
maintenance_tool.exe --backup JobName
```
Runs immediate backup regardless of schedule.

## Technical Implementation Details

### File Format Changes
**Config.txt structure updated:**
```
[job:Name]
sources=...
destinations=...
schedule_enabled=1
schedule_interval=daily
schedule_value=1
schedule_time=02:00
schedule_dest=D:/SchedulePath         # NEW
schedule_onetime=0                     # NEW
additional_schedules=...               # NEW
last_run=1733000000
```

### Code Changes
1. **Job struct** - Added `scheduleDest`, `scheduleOneTime`, `additionalSchedules`
2. **loadConfig()** - Parses new fields
3. **saveConfig()** - Writes new fields
4. **addJobInteractive()** - Prompts for schedule destination, one-time, additional schedules
5. **shouldRunScheduled()** - Handles "once" interval type and one-time flag
6. **runScheduler()** - Processes additional schedules, manages destinations, updates timestamps
7. **listJobs()** - Displays schedule details and additional schedules

### Backward Compatibility
- Old config files without new fields will work (fields default to empty/false)
- Existing jobs upgraded by adding new fields on next save
- No breaking changes to existing functionality

## Best Practices

1. **Use schedule-specific destinations** for different retention policies
   - Fast local SSD for hourly backups
   - Network drive for daily backups
   - Archive drive for weekly/monthly

2. **Combine intervals intelligently**
   - Frequent small backups (hourly incremental)
   - Less frequent full backups (daily/weekly)
   - Long-term archives (monthly)

3. **One-time schedules** for special events
   - Project milestones
   - End-of-quarter reports
   - Pre-deployment backups

4. **Monitor logs** to verify schedules execute
   ```
   2025-11-30 02:00:15 | scheduler | MyJob | triggered
   2025-11-30 02:01:42 | scheduler-additional | MyJob schedule=1 | triggered
   ```

5. **Test schedule logic** before production
   - Use short intervals (mins) for testing
   - Verify destinations are created correctly
   - Confirm one-time schedules disable properly

## Troubleshooting

### Schedule Not Running
- Check `schedule_enabled=1` in config.txt
- Verify `last_run` timestamp is updating
- Review maintenance.log for errors
- Ensure daemon/scheduler is actually running

### Wrong Destination
- Verify `schedule_dest` is set correctly
- Check additional_schedules format (pipe-delimited)
- Confirm paths exist and are writable

### One-Time Schedule Running Multiple Times
- Check `schedule_onetime=1` in config
- Verify schedule hasn't been re-enabled manually
- Review "once" vs recurring interval types

### Additional Schedules Not Working
- Validate pipe-delimited format: `interval|value|time|dest|onetime|lastrun`
- Ensure no extra spaces in config file
- Check if marked as "disabled" in schedule string
