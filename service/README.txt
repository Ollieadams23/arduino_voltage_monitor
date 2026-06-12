SERVICE FOLDER - File Descriptions
====================================

STARTUP & INSTALLATION:
-----------------------
install_startup_task.bat
  - Registers a Windows Task Scheduler task to run start_service.vbs at login
  - Requires admin privileges
  - Creates hidden startup task "ESP32_Voltage_Monitor"

start_service.vbs
  - Launches ALL three Python services hidden (no console windows)
  - Used by Task Scheduler for automatic startup at login
  - Can also be double-clicked to manually start services
  - Launches: pc_receiver.py, git_sync.py, tray_status.py

start_tray_status.cmd
  - Batch file to launch only the tray status monitor
  - May show a console window

remove_startup_and_kill.bat
  - Removes the Task Scheduler startup task
  - Kills all running Python services
  - Requires admin privileges

PYTHON SERVICES:
----------------
pc_receiver.py
  - Receives voltage data from Arduino ESP32 over network
  - Logs data to data/history.jsonl
  - Saves latest reading to data/latest.json
  - Runs as a background service

git_sync.py
  - Periodically syncs project files to a Git repository
  - Automatically commits and pushes changes
  - Logs activity to data/git_sync.log

tray_status.py
  - System tray application showing voltage monitor status
  - Provides quick access to monitor and controls
  - Runs in system notification area

test_receiver.py
  - Testing utility for pc_receiver.py
  - Used for debugging and development

DOCUMENTATION:
---------------
PC_RECEIVER_SETUP.md
  - Setup instructions for the PC receiver service

DATA FOLDER:
-----------
data/
  - history.jsonl: Log of all voltage readings
  - latest.json: Most recent voltage reading
  - *.log files: Service activity logs

SETUP INSTRUCTIONS:
-------------------
1. Configure Python environment
2. Run: install_startup_task.bat (as Administrator)
3. Services will start automatically at next login
4. Or manually run: start_service.vbs

REMOVAL INSTRUCTIONS:
---------------------
Run: remove_startup_and_kill.bat (as Administrator)
This will stop all services and remove the startup task.
