@echo off
REM Scheduler daemon starter for Business Maintenance Tool
REM Add this to Windows Task Scheduler to run at startup

cd /d "%~dp0"

echo Starting Maintenance Tool Scheduler Daemon...
echo Checking for scheduled backups every 60 minutes
echo Press Ctrl+C to stop
echo.

maintenance_tool.exe --daemon 60

REM If daemon exits, log it
echo Scheduler daemon stopped at %DATE% %TIME% >> scheduler.log
