#!/bin/bash
# Grab clipboard image on macOS and save as PNG.
outPath="$1"
if [ -z "$outPath" ]; then
    echo "Usage: grab_clipboard.sh <output.png>"
    exit 1
fi

osascript -e '
set theFile to POSIX file "'"$outPath"'"
try
    set theImage to the clipboard as «class PNGf»
on error
    return "no image"
end try
set fh to open for access theFile with write permission
write theImage to fh
close access fh
return "ok"
' 2>/dev/null | grep -q "ok"

if [ $? -eq 0 ]; then
    echo "Saved to $outPath"
    exit 0
else
    echo "No image on clipboard"
    exit 1
fi
