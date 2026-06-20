' Double-click launcher for the Grimoire & Steel SQL console.
' Runs the Tkinter app with a hidden console window (the GUI window still shows).
Option Explicit
Dim fso, shell, here, target
Set fso = CreateObject("Scripting.FileSystemObject")
Set shell = CreateObject("WScript.Shell")
here = fso.GetParentFolderName(WScript.ScriptFullName)
target = here & "\db_console.pyw"
' arg2 = 0 hides the console window; arg3 = False = don't wait.
shell.Run "py """ & target & """", 0, False
