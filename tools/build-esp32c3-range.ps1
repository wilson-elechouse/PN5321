param(
    [string]$ArduinoCli = 'D:\GreenSoftware\Arduino_cli\arduino-cli.exe'
)
$ErrorActionPreference = 'Stop'
if (!(Test-Path -LiteralPath $ArduinoCli)) {
    $command = Get-Command $ArduinoCli -ErrorAction SilentlyContinue
    if (!$command) { $command = Get-Command arduino-cli -ErrorAction SilentlyContinue }
    if (!$command) { throw 'Arduino CLI not found. Pass -ArduinoCli with its path.' }
    $ArduinoCli = $command.Source
}
$repo = Split-Path -Parent $PSScriptRoot
$fqbn = 'esp32:esp32:esp32c3:FlashMode=dio,FlashFreq=40,FlashSize=4M,CDCOnBoot=default,UploadSpeed=115200'
& $ArduinoCli compile --fqbn $fqbn --warnings all `
    --build-path (Join-Path $repo '.build\esp32c3-range') `
    --output-dir (Join-Path $repo '.artifacts\esp32c3-range') `
    (Join-Path $repo 'extras\esp32c3-range-test\PN532_All_Cards_UART_Range_Test')
if ($LASTEXITCODE -ne 0) { throw 'ESP32-C3 UART range test compilation failed.' }
