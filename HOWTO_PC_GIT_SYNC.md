# How To Set Up PC File Monitoring And Git Upload

This guide describes the simplest practical setup for sending data from the ESP32 to a Windows PC, watching for file changes, and pushing those changes to Git automatically.

## Overview

Recommended flow:

`ESP32 -> HTTP POST -> Windows PC receiver -> data/latest.json -> git commit/push`

Why this approach is better than pushing from the ESP32 directly:

- Git credentials stay on the PC
- HTTPS and Git complexity stay off the microcontroller
- The PC can handle retries, batching, validation, and deduping
- GitHub Pages can read the committed JSON file like any normal static asset

## What Runs On The PC

You need two automation pieces:

1. A receiver process
Receives voltage data from the ESP32 and writes a local file.

2. A sync process
Notices file changes and runs Git commands.

These can be two separate processes or one combined app. For simplicity, two small scripts are usually easiest to reason about.

## Suggested Folder Layout

Example inside this repo:

```text
voltage monitor/
  data/
    latest.json
    history.jsonl
  pc_tools/
    receiver.py
    sync_to_git.ps1
    start_receiver.ps1
    start_sync.ps1
```

Suggested file purposes:

- `data/latest.json`: most recent reading for a static site to fetch
- `data/history.jsonl`: optional append-only log for debugging or graph generation
- `receiver.py`: tiny HTTP server that writes incoming data files
- `sync_to_git.ps1`: watches for changes and commits/pushes them

## Data Format To Receive

Recommended JSON from the ESP32:

```json
{
  "timestamp": 1760000000,
  "voltage": 12.41,
  "threshold": 11.5,
  "deviceIp": "192.168.1.50",
  "deviceName": "esp32-voltage-monitor"
}
```

Keep the first version small. You can add graph history later.

## Receiver Setup

Recommended approach:

- Use Python on the PC
- Run a tiny HTTP server on a fixed local port such as `8787`
- Accept `POST /upload`
- Parse JSON
- Write `data/latest.json`
- Optionally append one line to `data/history.jsonl`

Receiver requirements:

- reject invalid JSON
- avoid partial file writes
- log requests and errors
- return `200 OK` on success
- bind to the PC's LAN IP or all interfaces if needed

## File Monitoring And Git Sync

Two good strategies exist.

### Option A: Immediate Watcher

Use a watcher that notices file changes and then:

- waits a short debounce period such as 10 to 30 seconds
- checks whether the file content actually changed
- runs `git add data/latest.json`
- optionally adds `data/history.jsonl`
- commits with a fixed message
- pushes to origin

Best when updates are not too frequent.

### Option B: Timed Sync Loop

Run a loop every few minutes that:

- checks whether tracked data files changed
- commits only when needed
- pushes only when there is a new commit

Best when the ESP32 may update often and you want fewer commits.

For this project, a timed sync loop every 5 minutes is usually safer than pushing every single update.

## Git Setup On The PC

Before automating pushes, confirm this repo can already push without asking for credentials each time.

Recommended options:

### HTTPS with Git Credential Manager

- install Git for Windows if not already installed
- ensure Git Credential Manager is enabled
- run one manual push and complete sign-in once

### SSH

- create an SSH key on the PC
- add the public key to GitHub
- switch the repo remote to SSH
- test a manual push

Whichever method you use, make sure automation can push with no interactive prompt.

## Windows Startup Across Restarts

Because you want this to survive restarts, use Task Scheduler.

### Receiver Startup Task

Create a scheduled task that:

- runs `python receiver.py`
- triggers `At startup` or `At log on`
- starts in the repo or `pc_tools` folder
- runs whether the user is logged in or not if needed
- restarts on failure if the process exits

### Git Sync Startup Task

Create a second scheduled task that:

- runs `powershell.exe -ExecutionPolicy Bypass -File .\pc_tools\start_sync.ps1`
- triggers `At startup` or `At log on`
- starts after a short delay such as 30 to 60 seconds
- restarts on failure

If the sync is only a periodic script and not a long-running watcher, you can instead create one scheduled task that runs every 5 minutes.

## Recommended Startup Choice

For this project:

- receiver: long-running process started at Windows startup
- git sync: scheduled every 5 minutes or long-running watcher with debounce

That avoids missing data and keeps commit volume reasonable.

## Networking Notes

The ESP32 must know how to reach the PC.

Best options:

- use the PC's local hostname if your network resolves it reliably
- or use a reserved LAN IP for the PC from your router

Avoid relying on a random changing PC IP.

Example receiver URL:

```text
http://YOUR-PC-NAME:8787/upload
```

or

```text
http://192.168.1.100:8787/upload
```

## Reliability Recommendations

- do not push on every one-second measurement
- upload from the ESP32 every 1 to 5 minutes instead
- debounce file writes before committing
- skip commits when content is unchanged
- keep the local dashboard independent so the monitor still works if the PC is off
- log receiver errors to a file for troubleshooting

## Security Recommendations

Even on a home LAN, add at least minimal protection.

Suggested first step:

- include a simple shared token in the ESP32 upload request
- have the PC receiver reject requests with the wrong token

Also:

- do not expose the PC receiver directly to the public internet
- do not store GitHub personal access tokens in the ESP32
- keep Git credentials only on the PC

## What The Static Site Can Read

Once the PC pushes the updated file, a static site can fetch something like:

```text
/data/latest.json
```

This works well for:

- current voltage
- threshold
- last update time
- online or stale status

If you later want charts, also push a second history file.

## Best Practical First Version

Build the smallest working system first:

1. ESP32 uploads a small JSON payload every 5 minutes
2. PC receiver writes `data/latest.json`
3. PC sync task runs every 5 minutes
4. Sync task commits only when `data/latest.json` changed
5. Static page reads that JSON file

That is much easier to support than a full live streaming system.

## Manual Test Plan

1. Start the receiver on the PC
2. Send a test JSON payload manually from the PC or ESP32
3. Confirm `data/latest.json` updates
4. Run the sync script manually once
5. Confirm commit and push succeed
6. Reboot Windows
7. Confirm the receiver and sync process start automatically
8. Send another test payload after reboot
9. Confirm the repo updates again without manual intervention

## Future Improvements

- append and publish a history file for charting
- add local retry queueing if GitHub is unavailable
- generate a `status.json` file with health info
- batch multiple readings into one commit
- move from Task Scheduler to a Windows service wrapper if you want a more appliance-like setup