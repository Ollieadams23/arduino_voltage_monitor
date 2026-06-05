"""Windows service entry point for pc_receiver.py."""

import win32serviceutil

from old.windows_service_host import ScriptService


class PCReceiverService(ScriptService):
    _svc_name_ = "ESP32VoltageReceiver"
    _svc_display_name_ = "ESP32 Voltage Receiver"
    _svc_description_ = "Receives voltage data from the ESP32 and writes local JSON files."
    _svc_reg_class_ = "pc_receiver_service.PCReceiverService"
    _script_name_ = "pc_receiver.py"


if __name__ == "__main__":
    win32serviceutil.HandleCommandLine(PCReceiverService)