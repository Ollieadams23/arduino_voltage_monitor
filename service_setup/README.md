# Service Setup

This folder contains the files used to install and manage the Windows services for the local PC side of the project.

Included files:
- `install_services.cmd` and `install_services.ps1` - install or update the receiver and git sync services
- `uninstall_services.cmd` and `uninstall_services.ps1` - remove the services
- `pc_receiver_service.py` - Windows service entry point for `pc_receiver.py`
- `git_sync_service.py` - Windows service entry point for `git_sync.py`
- `windows_service_host.py` - shared service host used by both services

Recommended usage from an elevated PowerShell window:

```powershell
cd "C:\Users\Ollie\Documents\projects\voltage monitor\service_setup"
.\install_services.cmd
```

To remove the services later:

```powershell
cd "C:\Users\Ollie\Documents\projects\voltage monitor\service_setup"
.\uninstall_services.cmd
```

Notes:
- The actual receiver logic stays in `..\pc_receiver.py`.
- The actual git sync logic stays in `..\git_sync.py`.
- Root-level `install_services.*` and `uninstall_services.*` files are thin wrappers that forward to this folder.