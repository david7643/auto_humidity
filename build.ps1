param([switch]$RepackWeb)
$ErrorActionPreference = 'Stop'
$env:ARDUINO_DIRECTORIES_USER = Join-Path $PSScriptRoot '.tools\user'
$cli = Join-Path $PSScriptRoot '.tools\arduino-cli\arduino-cli.exe'
if (!(Test-Path -LiteralPath $cli)) { throw 'Run setup.ps1 first.' }
if ($RepackWeb) {
  & python (Join-Path $PSScriptRoot 'scripts\pack_web.py')
  if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}
# Relax AVR calls/jumps to recover flash space without removing UI/IR features.
& $cli compile --fqbn arduino:avr:uno --warnings all --build-property 'compiler.c.elf.extra_flags=-Wl,--relax' --build-path (Join-Path $PSScriptRoot '.tools\build') $PSScriptRoot
exit $LASTEXITCODE
