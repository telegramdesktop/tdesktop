# Create gittare/lib_ui and Push Amharic Font Changes

**Run this BEFORE pushing tdesktop** so that the lib_ui submodule can be fetched.

## Steps

1. **Log in to GitHub** (one-time):
   ```
   gh auth login
   ```
   Follow the prompts (browser or token).

2. **Create repo and push**:
   ```
   .\create-lib-ui-fork-and-push.bat
   ```
   Or: `powershell -File create-lib-ui-fork-and-push.ps1`

3. After success, https://github.com/gittare/lib_ui will exist with the Amharic font fix.

4. Then push tdesktop: `git push fork fix/amharic-font-rendering`
