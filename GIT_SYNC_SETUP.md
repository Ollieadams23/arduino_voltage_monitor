# Git Automation Setup Guide

This guide explains how to automatically commit and push `data/latest.json` to GitHub whenever the ESP32 uploads new data.

## Overview

The workflow:
1. **ESP32** uploads voltage data to PC receiver (every 5 minutes)
2. **PC receiver** writes `data/latest.json`
3. **Git sync script** detects the change
4. **Git sync script** commits and pushes to GitHub
5. **GitHub Pages** serves the updated data to your static site

## Prerequisites

Before starting, make sure:
- ✓ PC receiver is working (`pc_receiver.py` tested and running)
- ✓ You have a GitHub repository for this project
- ✓ Git is installed and configured on your PC
- ✓ You can push to the repo without entering a password

## Step 1: Configure Git Credentials

The script needs to push without password prompts. Choose one method:

### Option A: Git Credential Manager (Recommended for Windows)

Git Credential Manager is usually installed with Git for Windows.

1. **Test if it's working:**
   ```powershell
   git config --global credential.helper
   ```
   Should show: `manager` or `manager-core`

2. **If not set, configure it:**
   ```powershell
   git config --global credential.helper manager
   ```

3. **Make a test push** (enter password once):
   ```powershell
   git push
   ```
   Your credentials will be saved securely in Windows Credential Manager.

### Option B: SSH Keys

1. **Generate SSH key** (if you don't have one):
   ```powershell
   ssh-keygen -t ed25519 -C "your_email@example.com"
   ```
   Press Enter to accept defaults.

2. **Copy public key:**
   ```powershell
   Get-Content ~/.ssh/id_ed25519.pub | Set-Clipboard
   ```

3. **Add to GitHub:**
   - Go to GitHub → Settings → SSH and GPG keys
   - Click "New SSH key"
   - Paste the key and save

4. **Change remote to SSH:**
   ```powershell
   git remote set-url origin git@github.com:USERNAME/REPO.git
   ```

5. **Test:**
   ```powershell
   git push
   ```

### Option C: Personal Access Token (PAT)

1. **Generate token on GitHub:**
   - GitHub → Settings → Developer settings → Personal access tokens → Tokens (classic)
   - Generate new token with `repo` scope
   - Copy the token

2. **Use token as password:**
   ```powershell
   git push
   ```
   Username: your GitHub username
   Password: paste the token

   Credential Manager will save it.

## Step 2: Install Dependencies

```powershell
python -m pip install watchdog
```

Or install all dependencies:
```powershell
python -m pip install -r requirements.txt
```

## Step 3: Test Git Sync Manually

**Start the Git sync script:**
```powershell
python git_sync.py
```

You should see:
```
============================================================
ESP32 Voltage Monitor - Git Auto-Sync Starting
============================================================
Git version: git version 2.x.x
Git repository detected
Git remote:
origin  https://github.com/USERNAME/REPO.git (fetch)
origin  https://github.com/USERNAME/REPO.git (push)
Watching for changes: data\latest.json
Debounce time: 10 seconds
Press Ctrl+C to stop
============================================================
```

**Trigger a test upload:**

In another PowerShell window:
```powershell
python test_receiver.py
```

The git sync script should detect the change and push:
```
Detected change to latest.json
Starting Git sync...
Staged data/latest.json
Committed: Update voltage data: 12.450V at 2026-06-04 12:34:56
Pushed to remote repository
✓ Sync complete - Voltage: 12.450V
```

## Step 4: Run Both Scripts Together

You'll need to run two scripts continuously:
1. `pc_receiver.py` - Receives data from ESP32
2. `git_sync.py` - Pushes changes to Git

**Option A: Two Terminal Windows**

Terminal 1:
```powershell
python pc_receiver.py
```

Terminal 2:
```powershell
python git_sync.py
```

**Option B: Combined PowerShell Script**

I can create a launcher script that runs both together.

## Step 5: Windows Startup Automation

### Method 1: Task Scheduler (Two Tasks)

Create two scheduled tasks (similar to PC receiver setup):

**Task 1: PC Receiver**
- Name: `ESP32 Voltage Receiver`
- Program: `python`
- Arguments: `pc_receiver.py`
- Start in: `C:\Users\Ollie\Documents\projects\voltage monitor`

**Task 2: Git Sync**
- Name: `ESP32 Git Sync`
- Program: `python`
- Arguments: `git_sync.py`
- Start in: `C:\Users\Ollie\Documents\projects\voltage monitor`
- **Important:** Set trigger delay to **1 minute** (wait for receiver to start)

### Method 2: Combined Launcher Script

Create `start_all.bat`:
```batch
@echo off
cd /d "%~dp0"
start "ESP32 Receiver" python pc_receiver.py
timeout /t 5 /nobreak
start "ESP32 Git Sync" python git_sync.py
```

Then create one scheduled task pointing to `start_all.bat`.

## Configuration Options

Edit `git_sync.py` to customize behavior:

```python
# How long to wait after file change before committing (prevents spam)
DEBOUNCE_SECONDS = 10  # Default: 10 seconds

# Which file to watch
WATCHED_FILE = DATA_DIR / "latest.json"
```

## Troubleshooting

### Git sync says "Not in a Git repository"

Make sure you're in the project directory:
```powershell
cd "C:\Users\Ollie\Documents\projects\voltage monitor"
git status
```

Initialize Git if needed:
```powershell
git init
git remote add origin https://github.com/USERNAME/REPO.git
```

### Git push fails with authentication error

Run this test:
```powershell
git push
```

If it asks for password, your credentials aren't saved. See Step 1 above.

### "No changes to commit" every time

Check if `data/latest.json` is in `.gitignore`:
```powershell
git check-ignore data/latest.json
```

If it's ignored, remove it from `.gitignore`.

### Too many commits (spam)

Increase `DEBOUNCE_SECONDS` in `git_sync.py`:
```python
DEBOUNCE_SECONDS = 30  # Wait 30 seconds instead of 10
```

Or reduce ESP32 upload frequency (change from 5 minutes to 15 minutes in ESP32 settings).

### Git push is slow

This is normal - GitHub pushes take a few seconds. The script waits for completion.

If you see timeouts, check your internet connection.

## Files Created

- `git_sync.py` - Main watcher script
- `data/git_sync.log` - Git sync activity log

## Viewing Logs

**Git sync log:**
```powershell
Get-Content data\git_sync.log -Tail 20 -Wait
```

**Both logs side by side:**
```powershell
# Terminal 1
Get-Content data\receiver.log -Tail 10 -Wait

# Terminal 2
Get-Content data\git_sync.log -Tail 10 -Wait
```

## Testing the Complete Workflow

1. **Start both scripts**
2. **Trigger ESP32 upload** (or wait for next automatic upload)
3. **Check PC receiver log** - should show "Updated data/latest.json"
4. **Check Git sync log** - should show "✓ Sync complete"
5. **Check GitHub** - should see new commit
6. **Check GitHub Pages** - should see updated voltage (may take 1-2 minutes to deploy)

## Best Practices

- **Commit message format** - Includes voltage and timestamp for easy tracking
- **Debouncing** - Prevents rapid-fire commits if ESP32 uploads frequently
- **Error handling** - Continues watching even if a push fails
- **Logging** - All activity logged to `data/git_sync.log`

## Next Steps

After Git sync is working:
1. ✓ **Phase 4 complete!**
2. Set up GitHub Pages (if not already done)
3. Verify `index.html` can read `data/latest.json` from GitHub
4. Configure Windows startup automation (Phase 5)

## Advanced: Batch Multiple Commits

If you want to batch multiple readings into one commit (e.g., commit every 30 minutes instead of every upload):

Change in `git_sync.py`:
```python
DEBOUNCE_SECONDS = 1800  # 30 minutes in seconds
```

This way:
- ESP32 still uploads every 5 minutes
- PC receiver still updates `latest.json` every 5 minutes
- But Git only commits every 30 minutes

The file stays up-to-date locally, reducing Git spam while keeping data fresh.
