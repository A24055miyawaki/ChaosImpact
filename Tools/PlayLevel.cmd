@echo off
rem Plays a level at once, as the game in a window (not the editor).
rem   Drag a level file (Content\...\*.umap) onto this file, then pick how to play it.
rem   Or: PlayLevel.cmd <level .umap> [1|2|3]   (1 practice with one CPU, 2 RPG open world, 3 no options)
rem Save the level in the editor first (Ctrl+S): the game reads the saved file.
chcp 65001 >nul
setlocal EnableDelayedExpansion
rem The engine where the Epic launcher put it on this PC (the usual place if it says nothing).
set "ENGINE=C:\Program Files\Epic Games\UE_5.7"
for /f "skip=2 tokens=2,*" %%A in ('reg query "HKLM\SOFTWARE\EpicGames\Unreal Engine\5.7" /v InstalledDirectory 2^>nul') do set "ENGINE=%%B"
set "EDITOR=!ENGINE!\Engine\Binaries\Win64\UnrealEditor.exe"
if not exist "!EDITOR!" (
  echo Unreal Engine 5.7 が見つかりません: !EDITOR!
  pause
  exit /b 1
)
rem The project this file sits in (Tools\ is next to the .uproject), wherever it was cloned.
set "PROJECT=%~dp0..\ChaosImpact.uproject"
if "%~1"=="" (
  echo レベルのファイル（.umap）を、このファイルの上にドラッグ＆ドロップしてください。
  pause
  exit /b 1
)
rem C:\ChaosImpact\Content\Folder\Lvl_Name.umap  ->  /Game/Folder/Lvl_Name
set "FILE=%~f1"
set "REL=!FILE:*\Content\=!"
if "!REL!"=="!FILE!" (
  echo Content フォルダの中のレベルを選んでください: %~f1
  pause
  exit /b 1
)
set "REL=!REL:.umap=!"
set "MAP=/Game/!REL:\=/!"
set "MODE=%~2"
if "!MODE!"=="" (
  echo !MAP!
  echo 遊び方を選んでください
  echo   1: 練習（CPU 1体）
  echo   2: RPG（オープンワールド）
  echo   3: オプションなし
  choice /c 123 /n /m "番号: "
  set "MODE=!errorlevel!"
)
set "OPT="
if "!MODE!"=="1" set "OPT=?CITraining=1?CICPUCount=1?CITargets=0?CILocalPlayers=1"
if "!MODE!"=="2" set "OPT=?CIRPGPrototype=1?CIOpenWorld=1?CILocalPlayers=1"
start "" "!EDITOR!" "%PROJECT%" "!MAP!!OPT!" -game -windowed -ResX=1600 -ResY=900
