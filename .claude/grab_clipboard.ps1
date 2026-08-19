param([string]$outPath)
Add-Type -AssemblyName System.Windows.Forms
$img = [System.Windows.Forms.Clipboard]::GetImage()
if ($img) {
    $img.Save($outPath, [System.Drawing.Imaging.ImageFormat]::Png)
    Write-Host "Saved to $outPath"
    exit 0
} else {
    Write-Host "No image on clipboard"
    exit 1
}
