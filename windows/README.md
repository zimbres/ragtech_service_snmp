# Native Windows version

Target: Windows 10/11 or Windows Server with a working USB CDC serial driver.
This is a native console executable using Windows COM-port APIs and Winsock;
it needs no WSL, SNMP daemon, Python runtime, or third-party runtime libraries.
The MSVC build links its C runtime statically. The device's USB driver remains
required. UPS decoding, calibration, RFC1628 OIDs, diagnostic extensions and the
Zabbix template are shared with the Linux build.

## Build and create the ZIP

Install Visual Studio 2022 (or its Build Tools) with **Desktop development with
C++**, the Windows SDK, and CMake 3.21 or newer. Use Developer PowerShell:

```powershell
.\windows\build.ps1
```

It builds an x64 `build-windows\Release\ragtech_service.exe` and writes a portable
ZIP under `dist`. Python 3 is needed only if you run `build.ps1 -Test`.
Equivalent manual commands:

```powershell
cmake -S . -B build-windows -G "Visual Studio 17 2022" -A x64 -DBUILD_TESTING=OFF
cmake --build build-windows --config Release
cpack --config build-windows\CPackConfig.cmake -C Release -B dist
```

The repository also contains a GitHub Actions Windows build/test job. When this
repository is pushed to GitHub and the job succeeds, download the
`ragtech-windows-x64` artifact. This workflow is provided but is not run by the
local Linux workspace.

The ZIP contains the executable, Windows `ragtech.conf`, startup script, MIB and
Zabbix template/setup files. Extract it to a persistent directory; the executable
and config are under `bin`. It is not an MSI installer.

## Connect and run

1. Connect the UPS and find its COM port in Device Manager, under Ports (COM &
   LPT). If it is absent, install the appropriate device/CDC serial driver.
2. Stop the vendor monitoring program before running this agent: the COM port
   is opened exclusively.
3. Edit the supplied Windows `ragtech.conf`: set `port = COM3` (or your port),
   SNMP community, bind address and UDP port. `COM10` and higher are supported;
   the application adds the Windows device-path prefix automatically.
4. From the ZIP's `bin` directory, run:

   ```powershell
   .\ragtech_service.exe -c .\ragtech.conf -t
   .\ragtech_service.exe -c .\ragtech.conf -f -l debug
   ```

`-t` validates configuration without opening serial or binding UDP. Check actual
serial communication in the second command. Press Ctrl+C for graceful shutdown.
The application requests 2560 baud, 8N1 and verifies the driver's reported
settings. The USB driver's real handling of 2560 baud and the calibration
transactions still require validation with your UPS on Windows.

The default config path is `ragtech.conf` in the working directory. Energy totals
are stored at `%ProgramData%\Ragtech\energy.dat`; ensure the account can create
and write that directory. To use a different location, set a literal path under
`[general]`, for example `state_directory = C:/UPS/state` (create its parent
first). The INI parser does not expand environment variables in custom paths.
Only ANSI-compatible filesystem paths are supported by the Windows file APIs
used in this version.

Windows builds require `daemonize = false` and an empty `pid_file`. Linux daemon
and systemd options are unavailable. Console logs go to stderr; no Windows Event
Log integration is implemented. This executable does not implement the Windows
Service Control Manager protocol and cannot be installed directly with `sc.exe`.

## Automatic startup and firewall

After testing the COM port, open **elevated PowerShell** in the package's `bin`
directory and run:

```powershell
.\register-startup.ps1 -MonitoringSubnet '192.168.5.0/24'
```

Adjust the subnet to your Zabbix server/proxy network. If the INI uses another UDP
port, supply the matching `-SnmpPort`, for example `-SnmpPort 1161`.
The script registers the **Ragtech UPS SNMP** startup task as Local System,
allows it to continue on battery, enables restart attempts, creates the state
directory and adds an inbound UDP firewall rule on Domain/Private profiles for
the selected monitoring source. It starts the task immediately. The task uses
absolute executable/config paths; keep the package at that location.

Do not run a second agent manually while this task owns the COM port. For
foreground debugging, stop the task first with:

```powershell
Stop-ScheduledTask -TaskName 'Ragtech UPS SNMP'
```

The Task Scheduler stop action may forcibly terminate the process; foreground
Ctrl+C is the graceful shutdown path. Persisted energy can lag the last sample
by up to the save interval (60 seconds). The startup task retries failures three
times at one-minute intervals, so check the task status if the device is absent
for longer. Task Scheduler does not retain the console debug output.

To remove automatic startup and its firewall rule:

```powershell
Stop-ScheduledTask -TaskName 'Ragtech UPS SNMP'
Unregister-ScheduledTask -TaskName 'Ragtech UPS SNMP' -Confirm:$false
Get-NetFirewallRule -DisplayName 'Ragtech UPS SNMP' | Remove-NetFirewallRule
```

## Zabbix

Link **Ragtech UPS by SNMP** to a host whose SNMP interface points to the Windows
machine's IP address, configured UDP port and SNMP v2c community. The same
RFC1628 scaling and experimental OIDs work on both platforms. Leave the Windows
built-in SNMP service disabled on this address/port, or choose a different agent
port if it is already occupied.

## Validation

`build.ps1 -Test` runs platform-thread/lock/clock tests, Family 10 calibration and
XML-factor tests, native UDP GET/GETBULK tests, CLI/config tests, the SNMP protocol
suite and Zabbix template checks. Linux also exercises fragmented register reads
through a real pseudo-TTY; that emulator test skips on Windows. These tests use synthetic UPS data and do not
establish COM-driver compatibility with actual UPS hardware. The current Linux
workspace has no Windows compiler/SDK, so native Windows compilation and device
validation must be performed with the commands or workflow above.

Windows APIs used: [DCB / custom baud settings](https://learn.microsoft.com/en-us/windows/win32/api/winbase/ns-winbase-dcb),
[COMMTIMEOUTS](https://learn.microsoft.com/en-us/windows/win32/api/winbase/ns-winbase-commtimeouts),
[Winsock initialization](https://learn.microsoft.com/en-us/windows/win32/winsock/initializing-winsock).
