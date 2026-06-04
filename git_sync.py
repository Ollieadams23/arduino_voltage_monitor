"""
Git Auto-Sync for ESP32 Voltage Monitor

Watches data/latest.json for changes and automatically commits/pushes to Git.
Designed to run continuously alongside pc_receiver.py.
"""

import time
import json
import os
import subprocess
import logging
from pathlib import Path
from datetime import datetime
from watchdog.observers import Observer
from watchdog.events import FileSystemEventHandler

# Configuration
DATA_DIR = Path(__file__).parent / "data"
WATCHED_FILE = DATA_DIR / "latest.json"
LOG_FILE = DATA_DIR / "git_sync.log"
DEBOUNCE_SECONDS = 10  # Wait 10 seconds after change before committing

# Setup logging
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(levelname)s - %(message)s',
    handlers=[
        logging.FileHandler(LOG_FILE),
        logging.StreamHandler()
    ]
)
logger = logging.getLogger(__name__)


class GitSyncHandler(FileSystemEventHandler):
    """Handler for file system events on latest.json."""
    
    def __init__(self):
        self.last_sync_time = 0
        self.pending_sync = False
    
    def on_modified(self, event):
        """Called when a file is modified."""
        if event.is_directory:
            return
        
        # Only react to changes to latest.json
        if Path(event.src_path).name != WATCHED_FILE.name:
            return
        
        logger.info(f"Detected change to {WATCHED_FILE.name}")
        self.pending_sync = True
    
    def process_pending_sync(self):
        """Process pending sync if debounce time has elapsed."""
        if not self.pending_sync:
            return
        
        current_time = time.time()
        if current_time - self.last_sync_time < DEBOUNCE_SECONDS:
            return
        
        self.pending_sync = False
        self.last_sync_time = current_time
        sync_to_git()


def run_git_command(args, cwd=None):
    """Run a git command and return output."""
    try:
        result = subprocess.run(
            ['git'] + args,
            cwd=cwd or Path(__file__).parent,
            capture_output=True,
            text=True,
            timeout=30
        )
        return result.returncode, result.stdout, result.stderr
    except subprocess.TimeoutExpired:
        logger.error("Git command timed out")
        return -1, "", "Timeout"
    except Exception as e:
        logger.error(f"Error running git command: {e}")
        return -1, "", str(e)


def get_voltage_from_file():
    """Extract voltage value from latest.json for commit message."""
    try:
        with open(WATCHED_FILE, 'r') as f:
            data = json.load(f)
            return data.get('voltage', 'unknown')
    except Exception:
        return 'unknown'


def sync_to_git():
    """Commit and push latest.json to Git."""
    logger.info("Starting Git sync...")
    
    # Check if file exists
    if not WATCHED_FILE.exists():
        logger.warning(f"{WATCHED_FILE} does not exist, skipping sync")
        return

    relative_file = os.path.relpath(WATCHED_FILE, Path(__file__).parent)

    # Stage the file first so first-run/untracked cases are handled automatically.
    returncode, stdout, stderr = run_git_command(['add', '-f', '--', relative_file])
    if returncode != 0:
        logger.error(f"Git add failed: {stderr}")
        return
    
    # Check if there are changes to commit
    returncode, stdout, stderr = run_git_command(['status', '--porcelain', '--', relative_file])
    
    if returncode != 0:
        logger.error(f"Git status failed: {stderr}")
        return
    
    if not stdout.strip():
        logger.info(f"No changes to sync. {relative_file} is already tracked and Git sees no diff.")
        return
    
    # Get voltage for commit message
    voltage = get_voltage_from_file()
    timestamp = datetime.now().strftime('%Y-%m-%d %H:%M:%S')
    commit_message = f"Update voltage data: {voltage}V at {timestamp}"
    
    logger.info(f"Staged {relative_file}")
    
    # Commit
    returncode, stdout, stderr = run_git_command(['commit', '-m', commit_message])
    if returncode != 0:
        logger.error(f"Git commit failed: {stderr}")
        return
    
    logger.info(f"Committed: {commit_message}")
    
    # Push
    returncode, stdout, stderr = run_git_command(['push'])
    if returncode != 0:
        logger.error(f"Git push failed: {stderr}")
        logger.error("You may need to configure Git credentials or check network connection")
        return
    
    logger.info("Pushed to remote repository")
    logger.info(f"Sync complete - Voltage: {voltage}V")


def check_git_setup():
    """Verify Git is configured correctly."""
    logger.info("Checking Git configuration...")
    
    # Check if git is available
    returncode, stdout, stderr = run_git_command(['--version'])
    if returncode != 0:
        logger.error("Git is not installed or not in PATH")
        return False
    
    logger.info(f"Git version: {stdout.strip()}")
    
    # Check if we're in a git repo
    returncode, stdout, stderr = run_git_command(['rev-parse', '--git-dir'])
    if returncode != 0:
        logger.error("Not in a Git repository")
        return False
    
    logger.info("Git repository detected")
    
    # Check remote
    returncode, stdout, stderr = run_git_command(['remote', '-v'])
    if returncode != 0 or not stdout.strip():
        logger.warning("No Git remote configured")
    else:
        logger.info(f"Git remote:\n{stdout.strip()}")
    
    return True


def main():
    """Main entry point."""
    logger.info("="*60)
    logger.info("ESP32 Voltage Monitor - Git Auto-Sync Starting")
    logger.info("="*60)
    
    # Verify Git setup
    if not check_git_setup():
        logger.error("Git setup check failed. Please fix issues and restart.")
        return
    
    # Verify watched file exists (or its parent directory)
    if not DATA_DIR.exists():
        logger.error(f"Data directory does not exist: {DATA_DIR}")
        return
    
    logger.info(f"Watching for changes: {WATCHED_FILE}")
    logger.info(f"Debounce time: {DEBOUNCE_SECONDS} seconds")
    logger.info(f"Log file: {LOG_FILE}")
    logger.info("Press Ctrl+C to stop")
    logger.info("="*60)
    
    # Create file system observer
    event_handler = GitSyncHandler()
    observer = Observer()
    observer.schedule(event_handler, str(DATA_DIR), recursive=False)
    observer.start()
    
    try:
        while True:
            time.sleep(1)
            event_handler.process_pending_sync()
    except KeyboardInterrupt:
        logger.info("\nShutting down Git sync...")
        observer.stop()
    
    observer.join()
    logger.info("Git sync stopped")


if __name__ == "__main__":
    main()
