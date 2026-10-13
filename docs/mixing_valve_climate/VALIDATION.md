# Validation

Validated on 2026-10-13 with ESPHome **2026.8.2**, using the `esphome-builder`
container. No firmware was flashed and no live Home Assistant actions were sent.

## Executed checks

| Check | Result |
|---|---|
| Full ESPHome configuration loader, 6 schema test methods with subcases | PASS |
| Native host compilation and deterministic climate/valve integration tests | PASS |
| Generated `climate.control` and static/templated heat-source actions | PASS |
| HA heartbeat Jinja template, 16 connectivity/heating-state combinations | PASS |
| Existing valve suite: 13 schema methods, legacy and pulsed behavioral tests | PASS |
| ESP8266 nodemcuv2 example, Arduino framework | PASS |
| ESP32 esp32dev example, ESP-IDF 5.5.5 | PASS |

Reproduce with:

```sh
python tests/run.py
esphome compile examples/mixing_valve_climate/esp8266.yaml
esphome compile examples/mixing_valve_climate/esp32.yaml
```

For the controller suite only: `python tests/run.py mixing_valve_climate`.
Python needs ESPHome and its YAML/Jinja dependencies. The native suite uses an
isolated preferences directory and exits on assertion failure; execution has a
30-second timeout.

## Behavioral coverage

Tests use the real ESPHome Climate/Valve/Sensor/Automation classes and the real
`time_based_valve` component with a deterministic clock and recorded relay
outputs. They verify:

- Single target, AUTO/OFF traits, inclusive deadband boundaries and immediate
  reaction to measurements and changed setpoints.
- Asymmetric pulsed motion, stable pulse/pause deadlines under frequent updates,
  cancellation when entering the band, reversal, and no stale pulse resumption.
- Initially unknown heat source, CWU interruption, cooling while unavailable,
  heartbeat renewal with unchanged state, expiry, invalid dynamic states, and
  clock rollover without revival of expired state.
- Missing, NaN, infinite and silently expired temperature readings; recovery on
  a fresh valid sample; no opening while the input is invalid.
- Overtemperature during a pause, precedence over OFF/source loss, reset
  hysteresis, and inability of an invalid reading to clear protection.
- Continuous protective closure with endpoint overrun, no repeated overrun while
  an alarm persists, and correction of conflicting external opening commands.
- Both startup directions, waiting for normal homing, uninterrupted full closing
  calibration without an extra overrun, and full closure after aborting an
  opening calibration with unknown actual position.
- OFF cancellation and manual assistance, AUTO resumption, enforced setpoint
  limits, independent controllers, and restoring mode/target without restoring
  heat-source availability.

Schema checks cover references/types, required fields, finite/ordered temperature
limits, timeout bounds, supported modes, actions, duplicate valve ownership, and
rejection of the valve's external-button `manual_control` mode.

## Limits of validation

These checks establish software behavior and build compatibility for the stated
version. They do not simulate the hydraulics, demonstrate stable temperature
control, validate physical sensor placement, or establish safe temperature limits
for a floor installation. Relay latency, mechanical travel, thermal lag and actual
pulse settings require supervised commissioning.

The HA example was parsed and its template rendered against representative
inputs. It was not loaded into a running HA instance and its placeholder entities
and generated action name must be replaced. A heartbeat's freshness only confirms
message delivery; boiler telemetry validity is the sender's responsibility.
