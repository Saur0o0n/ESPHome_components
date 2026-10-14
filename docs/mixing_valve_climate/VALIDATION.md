# Validation

The subsequent manual-control change makes normal valve commands transition-only,
while retaining protection enforcement. No tests or compilation were run for that
change at the user's request. The results below describe the earlier revision.

Validated on 2026-10-14 with ESPHome **2026.8.2**, using the `esphome-builder`
container. No firmware was flashed and no live Home Assistant actions were sent.

## Executed checks

| Check | Result |
|---|---|
| Full ESPHome configuration loader, 8 schema test methods with subcases | PASS |
| Native host compilation and deterministic climate/valve integration tests | PASS |
| Generated `climate.control` and static/templated heat-source actions | PASS |
| HA heartbeat Jinja template, 16 connectivity/heating-state combinations | PASS |
| Existing valve suite: 14 schema methods, legacy and pulsed behavioral tests | PASS |
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

## Fast-closing and runtime-setting checks

Additional deterministic tests verify the 29°C target / +/-2°C tolerance /
5°C offset example: strict entry above 36°C, latching through 35–36°C and exit
at 35°C with 1°C hysteresis. They also exercise pause-to-continuous switching,
continuous-to-pulsed switching, cancellation in the idle band, pulsed-only opening,
OFF reset, immediate changes from HA number commands, rejection of invalid values,
independent controller settings, preference restoration and fallback after limits
change. Completed endpoint overrun is not restarted by a speed change.

The host configuration compiles and executes actual `number.set` actions. Both
MCU examples include the new number entities and API exposure.

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

## Optional heat source and direct HA import

The optional policy now uses fresh confirmed source states and falls back to local
regulation on missing, unknown, invalid or expired input. Unit checks cover known
off inhibiting opening, expiry releasing inhibition, unchanged off renewal, recovery,
normal/fast closing and the existing required-source behavior.

A second native host firmware imports a real `homeassistant` text sensor and runs
the documented package. An `aioesphomeapi` client simulates HA over the native TCP
protocol (no real HA instance is contacted). It verifies:

- an entity absent from startup, later appearance, and on/off state propagation;
- repeated requests refreshing an unchanged off value beyond the timeout;
- silent entity deletion expiring the last off state while still connected;
- raw HA unknown/unavailable/invalid text mapping to unknown, not confirmed off;
- disconnect/reconnect, including a separate logs-only client that stays connected;
- a constant subscription count across repeated polls and reconnects.

Both ESP32 and ESP8266 climate examples compile with the direct-import package.
The API transport test uses loopback port 6058 and temporary preferences; do not
run two copies of this test concurrently. It requires `aioesphomeapi`, provided by
the tested ESPHome environment. Actual HA entity IDs and boiler telemetry still
require validation on the running installation; no firmware was flashed.

## Heat-source diagnostic checks

Deterministic tests cover initially unknown age, unchanged-message renewal,
explicit unknown receipts, timeout, repeated local invalidation without an age
reset, updates while OFF, independent instances and elapsed time beyond a complete
32-bit clock cycle. Generated YAML exercises the invalidation action without API.
The native TCP test checks diagnostic entity categories and seconds units, effective
on/off/unknown states, receipt renewal, age after expiry, and age after disconnect.
