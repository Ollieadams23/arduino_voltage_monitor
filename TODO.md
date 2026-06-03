# ESP32 To PC To Git TODO

This file tracks the work needed to move voltage data from the ESP32 to a PC, write it to a local file, and automatically push updates to Git so a static site can read the latest data.

## Goal

Build an automation path like this:

`ESP32 -> PC receiver -> local data file -> git commit/push -> GitHub Pages or other static site`

## Recommended Design

- Use the ESP32 to send JSON to a small HTTP server running on the PC
- Have the PC server write a local file such as `data/latest.json`
- Use a watcher or timed sync script on the PC to detect changes
- Commit and push from the PC, not from the ESP32
- Start the receiver and sync process automatically after Windows restarts

## Phase 1: Define Data Format

- Decide the exact JSON payload the ESP32 should send
- Include at least:
  - timestamp
  - current voltage
  - alert threshold
  - device IP or device name
- Decide whether to send only the latest reading or also recent history
- Decide how often the ESP32 should upload data

## Phase 2: PC Receiver

- Create a small local HTTP receiver on the PC
- Accept `POST` requests from the ESP32
- Validate incoming JSON
- Write the payload to a predictable file path such as `data/latest.json`
- Optionally append a log file such as `data/history.jsonl`
- Return a simple success response to the ESP32

## Phase 3: File Change Handling

- Detect when `data/latest.json` changes
- Avoid committing unchanged or duplicate content
- Add optional debouncing so rapid updates do not create too many commits
- Decide whether to push:
  - immediately on change
  - on a fixed interval such as every 5 minutes
  - only when voltage changes by a meaningful amount

## Phase 4: Git Automation

- Confirm the repo can push without manual login prompts
- Configure Git Credential Manager or SSH keys on the PC
- Write an automated sync script that runs:
  - `git add`
  - `git commit`
  - `git push`
- Use a safe commit message format such as `Update voltage data`
- Make the script exit cleanly when there is nothing new to commit

## Phase 5: Windows Startup Automation

- Make sure the PC receiver starts automatically after reboot
- Make sure the Git sync watcher starts automatically after reboot
- Choose one startup method:
  - Task Scheduler at startup
  - Task Scheduler at logon
  - NSSM or Windows service wrapper for long-running processes
- Configure restart behavior if the receiver crashes

## Phase 6: ESP32 Firmware Changes

- Add HTTP client support to the sketch
- Add configuration for the PC receiver URL
- Add upload interval control
- Send JSON to the PC receiver
- Handle timeout or network failure without blocking the main monitor loop
- Keep local dashboard and email alerts working even if the PC is offline

## Phase 7: Static Site Consumption

- Point the static page at the committed JSON file in the repo
- Confirm GitHub Pages or other static host can read the file over HTTPS
- Decide whether the static page shows:
  - only latest voltage
  - last update time
  - threshold
  - recent history graph

## Reliability Checks

- Test with the PC turned off
- Test with the PC IP address changed
- Test with GitHub temporarily unreachable
- Test with invalid JSON from the ESP32
- Test restart behavior after a Windows reboot
- Test duplicate uploads to confirm the repo does not get spammed with noise commits

## Nice To Have

- Add a heartbeat field so the static site can show whether the device is stale
- Add batching so multiple readings become one commit
- Add local log rotation on the PC
- Add a second file for graph history instead of only `latest.json`
- Add basic authentication or a shared token between ESP32 and the PC receiver

## Completion Criteria

- ESP32 can upload voltage data to the PC reliably
- The PC updates a local JSON file automatically
- Changes are committed and pushed to Git automatically
- The automation survives Windows restarts
- A static page can read the pushed file without talking directly to the ESP32