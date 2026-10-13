# Run from an elevated PowerShell after editing ragtech.conf and testing the COM port.
# Installs a startup scheduled task; the executable is a console app, not an SCM service.
[CmdletBinding()]
param(
    [string]$Executable = (Join-Path $PSScriptRoot 'ragtech_service.exe'),
    [string]$Config = (Join-Path $PSScriptRoot 'ragtech.conf'),
    [ValidateRange(1,65535)][int]$SnmpPort = 161,
    [string]$MonitoringSubnet = 'LocalSubnet'
)
$ErrorActionPreference = 'Stop'
$Executable = (Resolve-Path -LiteralPath $Executable).Path
$Config = (Resolve-Path -LiteralPath $Config).Path
$TaskName = 'Ragtech UPS SNMP'
$Action = New-ScheduledTaskAction -Execute $Executable -Argument ('-c "{0}" -f' -f $Config) -WorkingDirectory (Split-Path $Executable)
$Trigger = New-ScheduledTaskTrigger -AtStartup
$Principal = New-ScheduledTaskPrincipal -UserId 'SYSTEM' -LogonType ServiceAccount -RunLevel Highest
$Settings = New-ScheduledTaskSettingsSet -RestartCount 3 -RestartInterval (New-TimeSpan -Minutes 1) `
    -ExecutionTimeLimit ([TimeSpan]::Zero) -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries -MultipleInstances IgnoreNew
New-Item -ItemType Directory -Path (Join-Path $env:ProgramData 'Ragtech') -Force | Out-Null
Register-ScheduledTask -TaskName $TaskName -Action $Action -Trigger $Trigger -Principal $Principal -Settings $Settings -Force | Out-Null
Get-NetFirewallRule -DisplayName $TaskName -ErrorAction SilentlyContinue | Remove-NetFirewallRule
New-NetFirewallRule -DisplayName $TaskName -Direction Inbound -Action Allow -Protocol UDP -LocalPort $SnmpPort `
    -Program $Executable -RemoteAddress $MonitoringSubnet -Profile Domain,Private | Out-Null
Start-ScheduledTask -TaskName $TaskName
Write-Host "Startup task and UDP $SnmpPort rule installed. Monitoring source: $MonitoringSubnet"
