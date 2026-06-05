"""Windows service entry point for git_sync.py."""

import win32serviceutil

from old.windows_service_host import ScriptService


class GitSyncService(ScriptService):
    _svc_name_ = "ESP32GitSync"
    _svc_display_name_ = "ESP32 Git Sync"
    _svc_description_ = "Watches data/latest.json and pushes changes to GitHub automatically."
    _svc_reg_class_ = "git_sync_service.GitSyncService"
    _script_name_ = "git_sync.py"


if __name__ == "__main__":
    win32serviceutil.HandleCommandLine(GitSyncService)