@echo off
setlocal

cd /d "%~dp0.."
powershell -ExecutionPolicy Bypass -File ".\tools\watch-scripts.ps1" -Configuration Debug -AttachDebugger
