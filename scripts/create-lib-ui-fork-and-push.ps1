# Create gittare/lib_ui and push Amharic font changes
# Run: gh auth login   (first time only)
# Then: .\scripts\create-lib-ui-fork-and-push.ps1

$ErrorActionPreference = "Stop"
$libUi = "c:\Users\User\telegram\tdesktop\Telegram\lib_ui"

# Check gh auth
$auth = & "C:\Program Files\GitHub CLI\gh.exe" auth status 2>&1
if ($LASTEXITCODE -ne 0) {
    Write-Host "Please run: gh auth login" -ForegroundColor Yellow
    Write-Host "Then run this script again." -ForegroundColor Yellow
    exit 1
}

# Create gittare/lib_ui by forking (run from lib_ui so gh knows the repo)
Write-Host "Creating gittare/lib_ui (fork of desktop-app/lib_ui)..." -ForegroundColor Cyan
Push-Location $libUi
& "C:\Program Files\GitHub CLI\gh.exe" repo fork desktop-app/lib_ui --remote=false 2>&1
$forkOk = $LASTEXITCODE -eq 0
Pop-Location
if (-not $forkOk) { exit 1 }

# Push our branch to the fork
Write-Host "Pushing fix/amharic-font-rendering to gittare/lib_ui..." -ForegroundColor Cyan
Push-Location $libUi
git remote remove fork 2>$null
git remote add fork https://github.com/gittare/lib_ui.git
git push fork fix/amharic-font-rendering
Pop-Location

Write-Host "Done. Check https://github.com/gittare/lib_ui" -ForegroundColor Green
