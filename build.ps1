param([switch]$RepackWeb)
$ErrorActionPreference = 'Stop'
$env:ARDUINO_DIRECTORIES_USER = Join-Path $PSScriptRoot '.tools\user'
$cli = Join-Path $PSScriptRoot '.tools\arduino-cli\arduino-cli.exe'
if (!(Test-Path -LiteralPath $cli)) { throw 'Run setup.ps1 first.' }
if ($RepackWeb) {
  & python (Join-Path $PSScriptRoot 'scripts\pack_web.py')
  if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}
& $cli compile --fqbn arduino:avr:uno --warnings all --build-path (Join-Path $PSScriptRoot '.tools\build') $PSScriptRoot
exit $LASTEXITCODE
