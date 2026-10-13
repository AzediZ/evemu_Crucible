@echo off
rem Opens the EVEmu Control Panel. Keep this file next to EVEmuControlPanel.ps1.
start "" powershell.exe -NoProfile -ExecutionPolicy Bypass -STA -WindowStyle Hidden -File "%~dp0EVEmuControlPanel.ps1"
