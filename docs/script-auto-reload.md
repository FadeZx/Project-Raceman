# Script Auto Rebuild

Project Raceman object scripts are compiled C++ files, so changes cannot be hot-swapped inside the currently running executable. Use the watcher to rebuild and restart the app automatically whenever a script is created or saved.

From the repository root:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\watch-scripts.ps1 -Configuration Debug
```

For a debug workflow that restarts and asks Visual Studio to attach to the new process:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\watch-scripts.ps1 -Configuration Debug -AttachDebugger
```

The watcher:

- builds `ProjectRaceman/Project Raceman.sln`
- starts `ProjectRaceman/bin/<Configuration>/ProjectRaceman.exe`
- watches `ProjectRaceman/assets/scripts/*.h` and `*.cpp`
- stops the running app, rebuilds, and starts it again after a script save
- with `-AttachDebugger`, invokes Visual Studio `vsjitdebugger.exe -p <pid>` for the restarted app

When you create a C++ script in the editor, the editor still generates the `.h` and `.cpp`, adds them to the Visual Studio project, regenerates `ScriptRegistry.cpp`, and attaches the script to the selected object. It also saves the scene after auto-attaching, so the attachment is still present after the watcher restarts the app.

If the rebuild fails, leave the watcher running, fix the compile error, and save the script again. If debugger attach fails, the app still starts; attach manually to the printed process id.
