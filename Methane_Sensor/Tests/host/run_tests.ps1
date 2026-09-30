$ErrorActionPreference = 'Stop'
$workspace = Resolve-Path (Join-Path $PSScriptRoot '..\..\..')
$project = Join-Path $workspace 'Methane_Sensor'
$output = Join-Path $PSScriptRoot 'test_firmware.exe'
$sources = @(
    (Join-Path $PSScriptRoot 'test_firmware.c'),
    (Join-Path $workspace 'APP\src\app_display.c'),
    (Join-Path $workspace 'APP\src\app_ir_counter.c'),
    (Join-Path $workspace 'APP\src\app_modbus.c'),
    (Join-Path $project 'Core\Src\bsp_byte_ring.c'),
    (Join-Path $project 'Core\Src\bsp_display_map.c'),
    (Join-Path $project 'Core\Src\port_display.c'),
    (Join-Path $project 'Core\Src\port_actuator.c'),
    (Join-Path $project 'Core\Src\port_ir_remote.c'),
    (Join-Path $project 'Core\Src\port_methane.c'),
    (Join-Path $project 'Core\Src\port_modbus.c'),
    (Join-Path $project 'Core\Src\port_gpio.c')
)
& 'D:\tool\bin\gcc.exe' -std=c11 -Wall -Wextra -Werror -O2 `
    '-I' (Join-Path $workspace 'APP\inc') `
    '-I' (Join-Path $workspace 'Device\inc') `
    '-I' (Join-Path $project 'Core\Inc') `
    '-I' (Join-Path $workspace 'Middleware\FreeRTOS\include') `
    '-I' (Join-Path $workspace 'Middleware\FreeRTOS\portable\GCC\ARM_CM3') `
    '-ffunction-sections' '-fdata-sections' '-Wl,--gc-sections' `
    @sources '-o' $output
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $output
exit $LASTEXITCODE
