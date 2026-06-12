Set objShell = CreateObject("WScript.Shell")
Set objFSO = CreateObject("Scripting.FileSystemObject")

' Get the absolute folder path of this running script safely
strCurrentDir = objFSO.GetParentFolderName(WScript.ScriptFullName)

' Force the background engine to look inside your folder
objShell.CurrentDirectory = strCurrentDir

' Launch the PC receiver invisibly (0 = hidden window)
objShell.Run "cmd /c python pc_receiver.py", 0, False

' Wait 5 seconds for receiver to start
WScript.Sleep 5000

' Launch the Git sync script invisibly
objShell.Run "cmd /c python git_sync.py", 0, False

' Launch the tray status monitor
objShell.Run "cmd /c python tray_status.py", 0, False

' Safely clear out the objects from your system memory
Set objShell = Nothing
Set objFSO = Nothing