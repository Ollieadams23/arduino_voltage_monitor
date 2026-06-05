"""Windows tray icon for ESP32 receiver and Git sync service status."""

from __future__ import annotations

import json
import subprocess
import threading
import time
import webbrowser
from datetime import datetime, timezone
from pathlib import Path

from PIL import Image, ImageDraw
import pystray


PROJECT_ROOT = Path(__file__).resolve().parent
DATA_DIR = PROJECT_ROOT / "data"
LATEST_FILE = DATA_DIR / "latest.json"
RECEIVER_LOG = DATA_DIR / "receiver.log"
SYNC_LOG = DATA_DIR / "git_sync.log"
RECEIVER_SERVICE = "ESP32VoltageReceiver"
SYNC_SERVICE = "ESP32GitSync"
REFRESH_SECONDS = 30
STALE_MINUTES = 15


class TrayStatusApp:
    def __init__(self):
        self.icon = pystray.Icon("esp32-voltage-monitor")
        self.status = self.collect_status()
        self.stop_event = threading.Event()
        self.refresh_thread = threading.Thread(target=self._refresh_loop, daemon=True)

    def run(self):
        self._apply_status()
        self.refresh_thread.start()
        self.icon.run()

    def stop(self):
        self.stop_event.set()
        self.icon.stop()

    def _refresh_loop(self):
        while not self.stop_event.wait(REFRESH_SECONDS):
            self.status = self.collect_status()
            self._apply_status()

    def _apply_status(self):
        self.icon.icon = self._build_icon(self.status["overall"])
        self.icon.title = self._build_title(self.status)
        self.icon.menu = pystray.Menu(
            pystray.MenuItem(lambda item: self._overall_label(), None, enabled=False),
            pystray.Menu.SEPARATOR,
            pystray.MenuItem(lambda item: self._receiver_label(), None, enabled=False),
            pystray.MenuItem("Start Receiver", lambda icon, item: self._service_action(RECEIVER_SERVICE, "start")),
            pystray.MenuItem("Stop Receiver", lambda icon, item: self._service_action(RECEIVER_SERVICE, "stop")),
            pystray.Menu.SEPARATOR,
            pystray.MenuItem(lambda item: self._sync_label(), None, enabled=False),
            pystray.MenuItem("Start Git Sync", lambda icon, item: self._service_action(SYNC_SERVICE, "start")),
            pystray.MenuItem("Stop Git Sync", lambda icon, item: self._service_action(SYNC_SERVICE, "stop")),
            pystray.Menu.SEPARATOR,
            pystray.MenuItem(lambda item: self._data_label(), None, enabled=False),
            pystray.MenuItem(lambda item: self._sync_time_label(), None, enabled=False),
            pystray.Menu.SEPARATOR,
            pystray.MenuItem("Open Receiver Status Page", lambda icon, item: webbrowser.open("http://localhost:52501")),
            pystray.MenuItem("Open Receiver Log", lambda icon, item: self._open_file(RECEIVER_LOG)),
            pystray.MenuItem("Open Git Sync Log", lambda icon, item: self._open_file(SYNC_LOG)),
            pystray.MenuItem("Refresh Now", lambda icon, item: self._force_refresh()),
            pystray.MenuItem("Quit", lambda icon, item: self.stop()),
        )
        self.icon.update_menu()

    def _force_refresh(self):
        self.status = self.collect_status()
        self._apply_status()

    def _service_action(self, service_name: str, action: str):
        subprocess.run(["sc", action, service_name], capture_output=True, text=True, creationflags=subprocess.CREATE_NO_WINDOW)
        time.sleep(1)
        self._force_refresh()

    def _open_file(self, file_path: Path):
        if file_path.exists():
            subprocess.Popen(["notepad.exe", str(file_path)])

    def _overall_label(self):
        return f"Overall: {self.status['overallLabel']}"

    def _receiver_label(self):
        return f"Receiver: {self.status['receiver']}"

    def _sync_label(self):
        return f"Git Sync: {self.status['sync']}"

    def _data_label(self):
        if self.status["lastVoltage"] is None:
            return "Latest reading: not available"
        age = self.status["dataAgeMinutes"]
        if age is None:
            return f"Latest reading: {self.status['lastVoltage']} V"
        return f"Latest reading: {self.status['lastVoltage']} V ({age} min ago)"

    def _sync_time_label(self):
        if not self.status["lastSyncAt"]:
            return "Last Git push: not available"
        return f"Last Git push: {self.status['lastSyncAt']}"

    def collect_status(self):
        receiver_state = self._query_service(RECEIVER_SERVICE)
        sync_state = self._query_service(SYNC_SERVICE)
        latest = self._read_latest_data()
        last_sync_at = self._read_last_sync_time()

        overall = "red"
        overall_label = "Action needed"

        both_running = receiver_state == "RUNNING" and sync_state == "RUNNING"
        data_age = latest.get("ageMinutes")
        data_fresh = data_age is not None and data_age <= STALE_MINUTES

        if both_running and data_fresh:
            overall = "green"
            overall_label = "Healthy"
        elif both_running:
            overall = "yellow"
            overall_label = "Running, waiting for fresh data"
        elif receiver_state == "NOT_INSTALLED" or sync_state == "NOT_INSTALLED":
            overall = "red"
            overall_label = "Service not installed"
        elif receiver_state == "RUNNING" or sync_state == "RUNNING":
            overall = "yellow"
            overall_label = "Partially running"

        return {
            "receiver": self._format_service_state(receiver_state),
            "sync": self._format_service_state(sync_state),
            "lastVoltage": latest.get("voltage"),
            "dataAgeMinutes": latest.get("ageMinutes"),
            "lastSyncAt": last_sync_at,
            "overall": overall,
            "overallLabel": overall_label,
        }

    def _query_service(self, service_name: str) -> str:
        result = subprocess.run(
            ["sc", "query", service_name],
            capture_output=True,
            text=True,
            timeout=10,
            creationflags=subprocess.CREATE_NO_WINDOW,
        )
        output = (result.stdout or "") + (result.stderr or "")
        if "FAILED 1060" in output:
            return "NOT_INSTALLED"
        if result.returncode != 0:
            return "UNKNOWN"

        for line in output.splitlines():
            if "STATE" in line:
                if "RUNNING" in line:
                    return "RUNNING"
                if "STOPPED" in line:
                    return "STOPPED"
                if "START_PENDING" in line:
                    return "START_PENDING"
                if "STOP_PENDING" in line:
                    return "STOP_PENDING"
        return "UNKNOWN"

    def _format_service_state(self, state: str) -> str:
        labels = {
            "RUNNING": "Running",
            "STOPPED": "Stopped",
            "START_PENDING": "Starting",
            "STOP_PENDING": "Stopping",
            "NOT_INSTALLED": "Not installed",
            "UNKNOWN": "Unknown",
        }
        return labels.get(state, state.title())

    def _read_latest_data(self):
        if not LATEST_FILE.exists():
            return {"voltage": None, "ageMinutes": None}

        try:
            with open(LATEST_FILE, "r", encoding="utf-8") as handle:
                data = json.load(handle)
        except Exception:
            return {"voltage": None, "ageMinutes": None}

        voltage = data.get("voltage")
        received_at = data.get("receivedAt")
        age_minutes = None

        if received_at:
            try:
                parsed = datetime.fromisoformat(received_at)
                if parsed.tzinfo is None:
                    parsed = parsed.replace(tzinfo=timezone.utc).astimezone()
                age_minutes = max(0, int((datetime.now(parsed.tzinfo) - parsed).total_seconds() // 60))
            except ValueError:
                age_minutes = None

        return {"voltage": voltage, "ageMinutes": age_minutes}

    def _read_last_sync_time(self):
        if not SYNC_LOG.exists():
            return None

        try:
            lines = SYNC_LOG.read_text(encoding="utf-8", errors="ignore").splitlines()
        except Exception:
            return None

        for line in reversed(lines):
            if "Sync complete - Voltage:" in line:
                return line[:19]
        return None

    def _build_title(self, status):
        return (
            f"ESP32 Monitor: {status['overallLabel']} | "
            f"Receiver: {status['receiver']} | "
            f"Git Sync: {status['sync']}"
        )

    def _build_icon(self, color_name: str):
        palette = {
            "green": (40, 160, 80),
            "yellow": (215, 170, 40),
            "red": (190, 60, 50),
        }
        color = palette.get(color_name, palette["red"])
        image = Image.new("RGBA", (64, 64), (0, 0, 0, 0))
        draw = ImageDraw.Draw(image)
        draw.ellipse((8, 8, 56, 56), fill=color, outline=(35, 35, 35), width=3)
        draw.rectangle((29, 18, 35, 46), fill=(255, 255, 255))
        draw.rectangle((18, 29, 46, 35), fill=(255, 255, 255))
        return image


def main():
    TrayStatusApp().run()


if __name__ == "__main__":
    main()