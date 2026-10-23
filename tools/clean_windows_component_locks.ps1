$ErrorActionPreference = "Continue"

Write-Host "Home-Display Windows component-lock cleanup"

$repo = Split-Path -Parent $PSScriptRoot
Set-Location $repo

Write-Host "Stopping stale build-related processes..."
Get-CimInstance Win32_Process | Where-Object {
    $_.Name -match '^(ninja|cmake|scons|platformio|pio|python)\w*\.exe$' -and
    $_.CommandLine -and $_.CommandLine.IndexOf($repo,[StringComparison]::OrdinalIgnoreCase) -ge 0
} | ForEach-Object {
        try {
            Write-Host "  stopping project process $($_.Name) pid=$($_.ProcessId)"
            Stop-Process -Id $_.ProcessId -Force -ErrorAction Stop
        } catch {}
}

Start-Sleep -Milliseconds 500

$targets = @(
    ".pio/build",
    ".pio/libdeps",
    "managed_components",
    "sdkconfig.home_display_p4_r3_dev",
    "sdkconfig.home_display_p4_r3_prod",
    "sdkconfig.home_display_p4_legacy_dev",
    "sdkconfig.home_display_p4_legacy_prod"
)

foreach ($target in $targets) {
    $path = [IO.Path]::GetFullPath((Join-Path $repo $target))
    if (-not $path.StartsWith($repo + [IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) { throw "Unsafe target: $path" }
    if (Test-Path -LiteralPath $path) {
        if ((Get-Item -LiteralPath $path -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) { Write-Warning "Skipping linked target $path"; continue }
        Write-Host "Removing $target ..."
        for ($i = 0; $i -lt 5; $i++) {
            try {
                Remove-Item -LiteralPath $path -Recurse -Force -ErrorAction Stop
                break
            } catch {
                if ($i -eq 4) {
                    Write-Warning "Could not remove $target. Close VS Code terminals, Explorer preview windows and antivirus scans, then retry."
                } else {
                    Start-Sleep -Milliseconds 500
                }
            }
        }
    }
}

Write-Host "Cleanup done."
Write-Host "Now run: python tools/dev.py build --env home_display_p4_legacy_dev"
