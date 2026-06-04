"""Shared Windows service host for long-running project scripts."""

from pathlib import Path
import subprocess
import sys

import servicemanager
import win32event
import win32service
import win32serviceutil


PROJECT_ROOT = Path(__file__).resolve().parent


class ScriptService(win32serviceutil.ServiceFramework):
    """Base class that runs one project script as a Windows service."""

    _script_name_ = None
    _script_args_ = ()

    def __init__(self, args):
        super().__init__(args)
        self.stop_event = win32event.CreateEvent(None, 0, 0, None)
        self.process = None

    def _build_command(self):
        if not self._script_name_:
            raise RuntimeError("_script_name_ must be set on the service class")

        script_path = PROJECT_ROOT / self._script_name_
        host_executable = Path(sys.executable)
        python_executable = host_executable.with_name("python.exe")
        executable = python_executable if python_executable.exists() else host_executable
        return [str(executable), str(script_path), *self._script_args_]

    def _log_info(self, message):
        servicemanager.LogInfoMsg(f"{self._svc_name_}: {message}")

    def _log_error(self, message):
        servicemanager.LogErrorMsg(f"{self._svc_name_}: {message}")

    def _stop_process(self):
        if not self.process or self.process.poll() is not None:
            return

        self.process.terminate()
        try:
            self.process.wait(timeout=15)
        except subprocess.TimeoutExpired:
            self.process.kill()
            self.process.wait(timeout=5)

    def SvcStop(self):
        self.ReportServiceStatus(win32service.SERVICE_STOP_PENDING)
        self._log_info("stop requested")
        self._stop_process()
        win32event.SetEvent(self.stop_event)

    def SvcDoRun(self):
        command = self._build_command()
        self._log_info(f"starting {' '.join(command)}")

        try:
            self.process = subprocess.Popen(
                command,
                cwd=str(PROJECT_ROOT),
                creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0),
            )
        except Exception as exc:
            self._log_error(f"failed to start child process: {exc}")
            raise

        self._log_info(f"child process started with PID {self.process.pid}")

        while True:
            wait_result = win32event.WaitForSingleObject(self.stop_event, 1000)
            if wait_result == win32event.WAIT_OBJECT_0:
                break

            return_code = self.process.poll()
            if return_code is not None:
                self._log_error(f"child process exited unexpectedly with code {return_code}")
                break

        self._stop_process()
        self._log_info("service stopped")