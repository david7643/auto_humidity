param([string]$Port = 'COM7')

$ErrorActionPreference = 'Stop'
$env:ARDUINO_DIRECTORIES_USER = Join-Path $PSScriptRoot '.tools\user'
$cli = Join-Path $PSScriptRoot '.tools\arduino-cli\arduino-cli.exe'
$build = Join-Path $PSScriptRoot '.tools\build'

& (Join-Path $PSScriptRoot 'build.ps1')
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& $cli upload --fqbn arduino:avr:uno --port $Port --input-dir $build $PSScriptRoot
exit $LASTEXITCODE
