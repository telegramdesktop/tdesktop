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

  if "!CommandPathUnix!" == "" (
    echo Provide source path.
    exit /b 1
  ) else if exist "SourceFiles\!CommandPathWin!.cpp" (
    echo This source already exists.
    exit /b 1
  )
  echo Generating source !CommandPathUnix!.cpp..
  mkdir "SourceFiles\!CommandPathWin!.cpp"
  rmdir "SourceFiles\!CommandPathWin!.cpp"

  call :write_comment !CommandPathWin!.cpp
  set "quote="""
  set "quote=!quote:~0,1!"
  set "source1=#include !quote!!CommandPathUnix!.h!quote!"
  (
    echo !source1!
    echo.
  )>> "SourceFiles\!CommandPathWin!.cpp"
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
    echo the official desktop version of Telegram messaging app, see https://telegram.org
    echo.
    echo Telegram Desktop is free software: you can redistribute it and/or modify
    echo it under the terms of the GNU General Public License as published by
    echo the Free Software Foundation, either version 3 of the License, or
    echo ^(at your option^) any later version.
    echo.
    echo It is distributed in the hope that it will be useful,
    echo but WITHOUT ANY WARRANTY; without even the implied warranty of
    echo MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
    echo GNU General Public License for more details.
    echo.
    echo In addition, as a special exception, the copyright holders give permission
    echo to link the code of portions of this program with the OpenSSL library.
    echo.
    echo Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
    echo Copyright ^(c^) 2014-2017 John Preston, https://desktop.telegram.org
    echo */
  )> "SourceFiles\!Path!"
  exit /b
)
