#!/usr/bin/env pwsh
# Iterative Task Runner
# Runs Claude Code in a loop to complete tasks from a taskplanner-created folder
#
# Usage: .\docs\ai\iterate.ps1 <featurename> [-MaxIterations N] [-Interactive] [-DryRun] [-SingleCommit] [-NoCommit]
#
# Arguments:
#   featurename     Name of the folder in .ai/ containing prompt.md and tasks.json
#   -MaxIterations  Maximum iterations before stopping (default: 50)
#   -Interactive    Pause between iterations for user confirmation (default: auto/no pause)
#   -DryRun         Show what would be executed without running
#   -SingleCommit   Don't commit after each task, commit all changes at the end
#   -NoCommit       Don't commit at all (no per-task commits, no final commit)

param(
    [Parameter(Position=0, Mandatory=$true)]
    [string]$FeatureName,

    [int]$MaxIterations = 50,
    [switch]$Interactive,
    [switch]$DryRun,
    [switch]$SingleCommit,
    [switch]$NoCommit
)

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepoRoot = Resolve-Path (Join-Path $ScriptDir "..\..")
$WorkDir = Join-Path $ScriptDir "work\$FeatureName"
$PromptMd = Join-Path $WorkDir "prompt.md"
$TasksJson = Join-Path $WorkDir "tasks.json"

$BuildOutputDir = Join-Path $RepoRoot "out\Debug"
$TelegramExe = Join-Path $BuildOutputDir "Telegram.exe"
$TelegramPdb = Join-Path $BuildOutputDir "Telegram.pdb"

function Format-Duration {
    param([int]$Seconds)

    if ($Seconds -lt 60) {
        return "${Seconds}s"
    } elseif ($Seconds -lt 3600) {
        $min = [math]::Floor($Seconds / 60)
        $sec = $Seconds % 60
        return "${min}m ${sec}s"
    } else {
        $hr = [math]::Floor($Seconds / 3600)
        $min = [math]::Floor(($Seconds % 3600) / 60)
        $sec = $Seconds % 60
        return "${hr}h ${min}m ${sec}s"
    }
}

function Test-BuildFilesUnlocked {
    $filesToCheck = @($TelegramExe, $TelegramPdb)

    foreach ($file in $filesToCheck) {
        if (Test-Path $file) {
            try {
                Remove-Item $file -Force -ErrorAction Stop
                Write-Host "Removed: $file" -ForegroundColor DarkGray
            }
            catch {
                Write-Host ""
                Write-Host "========================================" -ForegroundColor Red
                Write-Host "  ERROR: Cannot delete build output" -ForegroundColor Red
                Write-Host "  File is locked: $file" -ForegroundColor Red
                Write-Host "" -ForegroundColor Red
                Write-Host "  Please close Telegram.exe and any" -ForegroundColor Red
                Write-Host "  debugger, then try again." -ForegroundColor Red
                Write-Host "========================================" -ForegroundColor Red
                Write-Host ""
                return $false
            }
        }
    }
    return $true
}

function Show-ClaudeStream {
    param([string]$Line)

    try {
        $obj = $Line | ConvertFrom-Json -ErrorAction Stop

        switch ($obj.type) {
            "assistant" {
                if ($obj.message.content) {
                    foreach ($block in $obj.message.content) {
                        if ($block.type -eq "text") {
                            Write-Host $block.text -ForegroundColor White
                        }
                        elseif ($block.type -eq "tool_use") {
                            $summary = ""
                            if ($block.input) {
                                if ($block.input.file_path) {
                                    $summary = $block.input.file_path
                                } elseif ($block.input.pattern) {
                                    $summary = $block.input.pattern
                                } elseif ($block.input.command) {
                                    $cmd = $block.input.command
                                    if ($cmd.Length -gt 60) { $cmd = $cmd.Substring(0, 60) + "..." }
                                    $summary = $cmd
                                } else {
                                    $inputStr = $block.input | ConvertTo-Json -Compress -Depth 1
                                    if ($inputStr.Length -gt 60) { $inputStr = $inputStr.Substring(0, 60) + "..." }
                                    $summary = $inputStr
                                }
                            }
                            Write-Host "[Tool: $($block.name)] $summary" -ForegroundColor Yellow
                        }
                    }
                }
            }
            "user" {
                # Tool results - skip verbose output
            }
            "result" {
                Write-Host "`n--- Session Complete ---" -ForegroundColor Cyan
                if ($obj.cost_usd) {
                    Write-Host "Cost: `$$($obj.cost_usd)" -ForegroundColor DarkCyan
                }
            }
            "system" {
                # System messages - skip
            }
        }
    }
    catch {
        # Not valid JSON, skip
    }
}

# Verify feature folder exists
if (-not (Test-Path $WorkDir)) {
    Write-Error "Feature folder not found: $WorkDir`nRun '/taskplanner $FeatureName' first to create it."
    exit 1
}

# Verify required files exist
foreach ($file in @($PromptMd, $TasksJson)) {
    if (-not (Test-Path $file)) {
        Write-Error "Required file not found: $file"
        exit 1
    }
}

if ($SingleCommit -or $NoCommit) {
    $AfterImplementation = @"
   - Mark the task completed in tasks.json ("completed": true)
   - If new tasks emerged, add them to tasks.json
"@
    $CommitRule = "- Do NOT commit changes after task is done, just mark it as done in tasks.json. Commit will be done when all tasks are complete, separately."
} else {
    $AfterImplementation = @"
   - Mark the task completed in tasks.json ("completed": true)
   - Commit your changes
   - If new tasks emerged, add them to tasks.json
"@
    $CommitRule = ""
}

$Prompt = @"
You are an autonomous coding agent working on: $FeatureName

Read these files for context:
- .ai/$FeatureName/prompt.md - Detailed instructions and architecture
- .ai/$FeatureName/tasks.json - Task list with completion status

Do exactly ONE task per iteration.

## Steps

1. Read tasks.json and find the most suitable task to implement (it can be first uncompleted task or it can be some task in the middle, if it is better suited to be implemented right now, respecting dependencies)
2. Plan the implementation carefully
3. Implement that ONE task only
4. After successful implementation:
$AfterImplementation

## Critical Rules

- Only mark a task complete if you verified the work is done (build passes, etc.)
- If stuck, document the issue in the task's notes field and move on
- Do ONE task per iteration, then stop
- NEVER try to commit files in .ai/
$CommitRule

## Completion Signal

If ALL tasks in tasks.json have "completed": true, output exactly:
===ALL_TASKS_COMPLETE===
"@

$CommitPrompt = @"
You are an autonomous coding agent. All tasks for "$FeatureName" are now complete.

Your job: Create a single commit with all the changes.

## Steps

1. Run git status to see all modified files
2. Run git diff to review the changes
3. Create a commit with a short summary (aim for ~50 chars, max 76 chars) describing what was implemented
4. The commit message should describe the overall feature/fix, not list individual changes

## Critical Rules

- NEVER try to commit files in .ai/
- Use a concise commit message that captures the essence of the work done
"@

Write-Host ""
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "  Iterative Task Runner" -ForegroundColor Cyan
Write-Host "  Feature: $FeatureName" -ForegroundColor Cyan
Write-Host "  Max iterations: $MaxIterations" -ForegroundColor Cyan
Write-Host "  Mode: $(if ($Interactive) { 'Interactive' } else { 'Auto' })" -ForegroundColor Cyan
Write-Host "  Commit: $(if ($NoCommit) { 'None' } elseif ($SingleCommit) { 'Single (at end)' } else { 'Per task' })" -ForegroundColor Cyan
Write-Host "  Working directory: $RepoRoot" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

if ($DryRun) {
    Write-Host "[DRY RUN] Would execute with prompt:" -ForegroundColor Yellow
    Write-Host $Prompt
    Write-Host ""
    Write-Host "Feature folder: $WorkDir" -ForegroundColor Yellow
    Write-Host "Prompt file: $PromptMd" -ForegroundColor Yellow
    Write-Host "Tasks file: $TasksJson" -ForegroundColor Yellow
    exit 0
}

Push-Location $RepoRoot

$ScriptStartTime = Get-Date
$IterationTimes = @()

try {
    for ($i = 1; $i -le $MaxIterations; $i++) {
        Write-Host ""
        Write-Host "========================================" -ForegroundColor Yellow
        Write-Host "  Iteration $i of $MaxIterations" -ForegroundColor Yellow
        Write-Host "========================================" -ForegroundColor Yellow
        Write-Host ""

        if (-not (Test-BuildFilesUnlocked)) {
            exit 1
        }

        $IterationStartTime = Get-Date

        claude --dangerously-skip-permissions --verbose -p $Prompt --output-format stream-json 2>&1 | ForEach-Object {
            Show-ClaudeStream $_
        }

        $IterationEndTime = Get-Date
        $IterationDuration = [int]($IterationEndTime - $IterationStartTime).TotalSeconds
        $IterationTimes += $IterationDuration
        Write-Host "Iteration time: $(Format-Duration $IterationDuration)" -ForegroundColor DarkCyan

        # Check task status after each run
        $tasks = Get-Content $TasksJson | ConvertFrom-Json
        $incomplete = @($tasks.tasks | Where-Object { -not $_.completed })
        $inProgress = @($tasks.tasks | Where-Object { $_.started -and -not $_.completed })

        if ($incomplete.Count -eq 0) {
            if ($SingleCommit -and -not $NoCommit) {
                $i++
                if ($i -le $MaxIterations) {
                    Write-Host ""
                    Write-Host "========================================" -ForegroundColor Yellow
                    Write-Host "  Final commit iteration" -ForegroundColor Yellow
                    Write-Host "========================================" -ForegroundColor Yellow
                    Write-Host ""

                    $CommitStartTime = Get-Date

                    claude --dangerously-skip-permissions --verbose -p $CommitPrompt --output-format stream-json 2>&1 | ForEach-Object {
                        Show-ClaudeStream $_
                    }

                    $CommitEndTime = Get-Date
                    $CommitDuration = [int]($CommitEndTime - $CommitStartTime).TotalSeconds
                    $IterationTimes += $CommitDuration
                    Write-Host "Commit time: $(Format-Duration $CommitDuration)" -ForegroundColor DarkCyan
                } else {
                    Write-Host ""
                    Write-Host "========================================" -ForegroundColor Red
                    Write-Host "  Max iterations reached before commit" -ForegroundColor Red
                    Write-Host "  Run manually: git add . && git commit" -ForegroundColor Red
                    Write-Host "========================================" -ForegroundColor Red
                    Write-Host ""
                    exit 1
                }
            }

            $TotalTime = [int]((Get-Date) - $ScriptStartTime).TotalSeconds
            $AvgTime = if ($IterationTimes.Count -gt 0) { [int](($IterationTimes | Measure-Object -Sum).Sum / $IterationTimes.Count) } else { 0 }

            Write-Host ""
            Write-Host "========================================" -ForegroundColor Green
            Write-Host "  ALL TASKS COMPLETE!" -ForegroundColor Green
            Write-Host "  Feature: $FeatureName" -ForegroundColor Green
            Write-Host "  Iterations: $($IterationTimes.Count)" -ForegroundColor Green
            Write-Host "  Total time: $(Format-Duration $TotalTime)" -ForegroundColor Green
            Write-Host "  Avg per iteration: $(Format-Duration $AvgTime)" -ForegroundColor Green
            Write-Host "========================================" -ForegroundColor Green
            Write-Host ""

            exit 0
        }

        Write-Host ""
        Write-Host "Remaining tasks: $($incomplete.Count)" -ForegroundColor Cyan
        if ($inProgress.Count -gt 0) {
            Write-Host "In progress: $($inProgress[0].title)" -ForegroundColor Yellow
        }

        if ($Interactive) {
            Write-Host "Press Enter to continue, Ctrl+C to stop..." -ForegroundColor Cyan
            Read-Host
        } else {
            Start-Sleep -Seconds 2
        }
    }

    $TotalTime = [int]((Get-Date) - $ScriptStartTime).TotalSeconds
    $AvgTime = if ($IterationTimes.Count -gt 0) { [int](($IterationTimes | Measure-Object -Sum).Sum / $IterationTimes.Count) } else { 0 }

    Write-Host ""
    Write-Host "========================================" -ForegroundColor Red
    Write-Host "  Max iterations ($MaxIterations) reached" -ForegroundColor Red
    Write-Host "  Check tasks.json for remaining tasks" -ForegroundColor Red
    Write-Host "  Total time: $(Format-Duration $TotalTime)" -ForegroundColor Red
    Write-Host "  Avg per iteration: $(Format-Duration $AvgTime)" -ForegroundColor Red
    Write-Host "========================================" -ForegroundColor Red
    Write-Host ""
    exit 1
}
finally {
    Pop-Location
}
