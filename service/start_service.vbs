Set objShell = CreateObject("WScript.Shell")
Set objFSO = CreateObject("Scripting.FileSystemObject")

' Get the absolute folder path of this running script safely
strCurrentDir = objFSO.GetParentFolderName(WScript.ScriptFullName)

' Force the background engine to look inside your folder
objShell.CurrentDirectory = strCurrentDir

' Launch your Python scripts invisibly (, 0) without flashing terminal windows
objShell.Run "cmd /c python pc_receiver.py", 0, False

' Wait exactly 5 seconds (5000 milliseconds) for the receiver to catch its breath
WScript.Sleep 5000

' Launch your Git sync script invisibly (, 0)
objShell.Run "cmd /c python git_sync.py", 0, False

' Safely clear out the objects from your system memory
Set objShell = Nothing
Set objFSO = Nothing