$ErrorActionPreference = 'Stop'
$toolDir = Join-Path $PSScriptRoot '.tools'
$cliDir = Join-Path $toolDir 'arduino-cli'
$cli = Join-Path $cliDir 'arduino-cli.exe'
New-Item -ItemType Directory -Force $cliDir | Out-Null
if (!(Test-Path -LiteralPath $cli)) {
  $zipPath = Join-Path $toolDir 'arduino-cli.zip'
  & curl.exe -fL 'https://downloads.arduino.cc/arduino-cli/arduino-cli_latest_Windows_64bit.zip' -o $zipPath
  if ($LASTEXITCODE -ne 0) { throw 'Arduino CLI download failed.' }
  Expand-Archive -LiteralPath $zipPath -DestinationPath $cliDir -Force
}
$env:ARDUINO_DIRECTORIES_USER = Join-Path $toolDir 'user'
& $cli core update-index
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $cli core install arduino:avr@1.8.8
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $cli lib update-index
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $cli lib install Ethernet@2.0.2 IRremote@4.7.1
exit $LASTEXITCODE
