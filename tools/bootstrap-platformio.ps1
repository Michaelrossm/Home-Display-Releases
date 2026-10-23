$ErrorActionPreference = "Stop"
Write-Host "Home-Display PlatformIO bootstrap"
$py = Get-Command py -ErrorAction SilentlyContinue
if ($py) {
  & $py.Source -3 tools/dev.py setup
  & $py.Source -3 tools/dev.py doctor
} else {
  $python = Get-Command python -ErrorAction Stop
  & $python.Source tools/dev.py setup
  & $python.Source tools/dev.py doctor
}
Write-Host "Next: python tools/dev.py test"
Write-Host "Next: python tools/dev.py build"
