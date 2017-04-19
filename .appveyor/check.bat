@echo off

call:checkCommitMessage
GOTO:EOF

:checkCommitMessage
    call:logInfo "Commit message: %APPVEYOR_REPO_COMMIT_MESSAGE%"
    call:logInfo "Is pull request: %APPVEYOR_PULL_REQUEST_NUMBER%"

    if not "%APPVEYOR_PULL_REQUEST_NUMBER%" == "" (
        ECHO "%APPVEYOR_REPO_COMMIT_MESSAGE_EXTENDED%" | FINDSTR /C:"Signed-off-by: " >nul & IF ERRORLEVEL 1 (
            call:logError "The commit message does not contain the signature!"
            call:logError "More information: https://github.com/telegramdesktop/tdesktop/blob/master/.github/CONTRIBUTING.md#sign-your-work"
            exit 1
        ) else (
            call:logInfo "Commit message contains signature"

            :: Reset error level
            verify >nul
        )
    )
GOTO:EOF

:logInfo
    echo [INFO] %~1
GOTO:EOF

:logError
    echo [ERROR] %~1
GOTO:EOF
