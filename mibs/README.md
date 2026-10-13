# Ragtech MIB and Zabbix setup

`RAGTECH-UPS-MIB.txt` describes all 35 diagnostic extension objects implemented
by the agent: 13 metrics, 20 flags, connection state, and raw response bytes.
For the standard RFC1628 objects, use the existing IETF `UPS-MIB` module;
this extension deliberately does not redefine standard UPS objects.

The extensions use the unregistered experimental subtree `1.3.6.1.3.1628.1`.
All metric scalars are Integer32 in hundredths of physical units. Flags use
inactive(0)/active(1), and connection state uses disconnected(0)/connected(1).
Readings retain their last sample on disconnection; check the connection state.

## Load the MIBs

Zabbix loads MIBs through Net-SNMP on the **server or proxy that polls the UPS**.
A MIB file is installed on that machine, rather than imported through Zabbix's
XML/YAML template import screen. It defines object names and types; it does not
automatically create Zabbix items, triggers, or preprocessing.
See the [Zabbix MIB documentation](https://www.zabbix.com/documentation/current/en/manual/config/items/itemtypes/snmp/mibs).

1. Install your distribution's standard IETF MIB collection. Ensure `UPS-MIB`,
   `SNMPv2-SMI`, `SNMPv2-TC`, and `SNMPv2-CONF` are available to Net-SNMP.
2. Copy `RAGTECH-UPS-MIB.txt` to a Net-SNMP MIB search directory. For example:

   ```sh
   sudo install -m 0644 mibs/RAGTECH-UPS-MIB.txt /usr/share/snmp/mibs/
   ```

3. Add this line to `/etc/snmp/snmp.conf` (and remove/comment a bare `mibs :`
   directive if it disables loading):

   ```text
   mibs +UPS-MIB:RAGTECH-UPS-MIB
   ```

4. Validate name resolution, then restart the polling Zabbix server/proxy so it
   loads the new definitions:

   ```sh
   snmptranslate -m +UPS-MIB:RAGTECH-UPS-MIB -On UPS-MIB::upsBatteryVoltage.0
   # .1.3.6.1.2.1.33.1.2.5.0
   snmptranslate -m +UPS-MIB:RAGTECH-UPS-MIB -On RAGTECH-UPS-MIB::ragtechUpsBatteryVoltage.0
   # .1.3.6.1.3.1628.1.1.11.0
   sudo systemctl restart zabbix-server
   # Or restart zabbix-proxy on the machine polling the UPS.
   ```

For a local preview without installing the custom file:

```sh
snmptranslate -M +./mibs -m +UPS-MIB:RAGTECH-UPS-MIB -Tp
```

## Zabbix items

Configure a host SNMP interface using the UPS agent's address, UDP port, SNMP
version 2c and configured community. Create SNMP agent items using the following
symbolic OIDs (or their numeric equivalents from the main README). Use Numeric
(unsigned) for standard readings and flags, Numeric (float) for readings with
multipliers, and Character for manufacturer/model.

| SNMP OID | Unit / preprocessing |
| --- | --- |
| UPS-MIB::upsIdentManufacturer.0 | Character |
| UPS-MIB::upsIdentModel.0 | Character |
| UPS-MIB::upsOutputSource.0 | Enum: 1 other, 2 none, 3 mains, 4 bypass, 5 battery, 6 booster, 7 reducer |
| UPS-MIB::upsBatteryStatus.0 | Enum: 1 unknown, 2 normal, 3 low, 4 depleted |
| UPS-MIB::upsSecondsOnBattery.0 | Seconds |
| UPS-MIB::upsEstimatedMinutesRemaining.0 | Minutes |
| UPS-MIB::upsEstimatedChargeRemaining.0 | Percent |
| UPS-MIB::upsOutputPercentLoad.1 | Percent |
| UPS-MIB::upsBatteryVoltage.0 | V; custom multiplier 0.1 |
| UPS-MIB::upsOutputFrequency.0 | Hz; custom multiplier 0.1 |
| UPS-MIB::upsOutputCurrent.1 | A; custom multiplier 0.1 |
| UPS-MIB::upsOutputPower.1 | W; no multiplier |
| RAGTECH-UPS-MIB::ragtechUpsBatteryVoltage.0 | V; custom multiplier 0.01 |
| RAGTECH-UPS-MIB::ragtechUpsOutputFrequency.0 | Hz; custom multiplier 0.01 |
| RAGTECH-UPS-MIB::ragtechUpsOutputCurrent.0 | A; custom multiplier 0.01 |
| RAGTECH-UPS-MIB::ragtechUpsOutputPower.0 | W; custom multiplier 0.01 |
| RAGTECH-UPS-MIB::ragtechUpsConnected.0 | Enum: 0 disconnected, 1 connected |
| RAGTECH-UPS-MIB::ragtechUpsOnBattery.0 | Enum: 0 inactive, 1 active |

Apply custom multiplier **0.01 to every extension metric**, but not to flags,
connection state, or raw bytes. MIB `UNITS` declarations document scaling;
Zabbix preprocessing must still be configured explicitly.

The device does not report bypass mode. Standard readings return
`noSuchInstance` while disconnected; the runtime also returns that exception
when no estimate is available. Consider item/trigger dependencies on connection
state. Power is an estimate from voltage times current, not a measured real-power
reading. The main README lists all implemented OIDs and protocol limitations.
