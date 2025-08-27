@echo off
setlocal enabledelayedexpansion
set "FullScriptPath=%~dp0"
set "FullExecPath=%cd%"

set "Command=%1"
if "%Command%" == "test" (
  call :write_test %2
  exit /b %errorlevel%
) else if "%Command%" == "header" (
  call :write_header %2
  exit /b %errorlevel%
) else if "%Command%" == "source" (
  call :write_source %2
  exit /b %errorlevel%
) else if "%Command%" == "" (
  echo This is an utility for fast blank module creation.
  echo Please provide module path.
  exit /b
)

call :write_module %Command%
exit /b %errorlevel%

:write_module
(
  set "CommandPath=%1"
  set "CommandPathUnix=!CommandPath:\=/!"
  if "!CommandPathUnix!" == "" (
    echo Provide module path.
    exit /b 1
  )
  echo Generating module !CommandPathUnix!..
  call create.bat header !CommandPathUnix!
  call create.bat source !CommandPathUnix!
  exit /b
)

:write_header
(
  set "CommandPath=%1"
  set "CommandPathUnix=!CommandPath:\=/!"
  set "CommandPathWin=!CommandPath:/=\!"

  if "!CommandPathUnix!" == "" (
    echo Provide header path.
    exit /b 1
  ) else if exist "SourceFiles\!CommandPathWin!.h" (
    echo This header already exists.
    exit /b 1
  )
  echo Generating header !CommandPathUnix!.h..
  mkdir "SourceFiles\!CommandPathWin!.h"
  rmdir "SourceFiles\!CommandPathWin!.h"

  call :write_comment !CommandPathWin!.h
  set "header1=#pragma once"
  (
    echo !header1!
    echo.
  )>> "SourceFiles\!CommandPathWin!.h"
  exit /b
)

:write_source
(
  set "CommandPath=%1"
  set "CommandPathUnix=!CommandPath:\=/!"
  set "CommandPathWin=!CommandPath:/=\!"
  if "!CommandPathUnix:~-4!" == "_mac" (
    set "CommandExt=mm"
  ) else (
    set "CommandExt=cpp"
  )
  if "!CommandPathUnix!" == "" (
    echo Provide source path.
    exit /b 1
  ) else if exist "SourceFiles\!CommandPathWin!.!CommandExt!" (
    echo This source already exists.
    exit /b 1
  )
  echo Generating source !CommandPathUnix!.!CommandExt!..
  mkdir "SourceFiles\!CommandPathWin!.!CommandExt!"
  rmdir "SourceFiles\!CommandPathWin!.!CommandExt!"

  call :write_comment !CommandPathWin!.!CommandExt!
  set "quote="""
  set "quote=!quote:~0,1!"
  set "source1=#include !quote!!CommandPathUnix!.h!quote!"
  (
    echo !source1!
    echo.
  )>> "SourceFiles\!CommandPathWin!.!CommandExt!"
  exit /b
)

:write_test
(
  set "CommandPath=%1"
  set "CommandPathUnix=!CommandPath:\=/!"
  set "CommandPathWin=!CommandPath:/=\!"

  if "!CommandPathUnix!" == "" (
    echo Provide source path.
    exit /b 1
  ) else if exist "SourceFiles\!CommandPathWin!.cpp" (
    echo This source already exists.
    exit /b 1
  )
  echo Generating test !CommandPathUnix!.cpp..
  mkdir "SourceFiles\!CommandPathWin!.cpp"
  rmdir "SourceFiles\!CommandPathWin!.cpp"

  call :write_comment !CommandPathWin!.cpp
  set "quote="""
  set "quote=!quote:~0,1!"
  set "source1=#include !quote!catch.hpp!quote!"
  (
    echo !source1!
    echo.
  )>> "SourceFiles\!CommandPathWin!.cpp"
  exit /b
)

:write_comment
(
  set "Path=%1"
  (
    echo /*
    echo This file is part of Telegram Desktop,
    echo the official desktop application for the Telegram messaging service.
    echo.
    echo For license and copyright information please follow this link:
    echo https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
    echo */
  )> "SourceFiles\!Path!"
  exit /b
)
