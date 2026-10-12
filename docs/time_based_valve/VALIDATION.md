# Validation — 2026-10-12

Environment: local `esphome-builder` container, ESPHome **2026.8.2**.
Sources were copied to a temporary directory inside the container for testing.
No existing device configurations were changed and no firmware was flashed.

| Check | Result |
|---|---|
| `esphome config examples/time_based_valve/esp8266.yaml` | PASS |
| `python tests/run.py`: configuration validation | PASS — 10 test methods, including subcases |
| `python tests/run.py`: host compilation | PASS — platformio/native 1.2.1 |
| `python tests/run.py`: C++ behavioral tests | PASS |
| YAML-generated actions: control/stop/open/close/toggle | PASS |
| `esphome compile examples/time_based_valve/esp8266.yaml` | PASS — nodemcuv2, Arduino 3.1.2 |

The ESP8266 build includes two valves, four interlocked GPIO switches,
Wi-Fi and the native API. Both valves use `start_position: closed` and
`end_positions_overrun: 5%`. Program size depends on the configuration and ESPHome version.

The C++ tests use ESPHome's actual `Valve`, `ValveCall`, `Trigger`,
`Automation` and preferences implementations. A controlled test clock replaces
the real clock, and in-memory counters/states replace actuator outputs. Coverage includes:

- first startup at 50%, restoring a saved position without movement and rejecting NaN;
- avoiding a preferences write for every position update during movement;
- moving from 20% to 50% in 36 s with a full travel time of 120 s;
- separate opening and closing durations;
- STOP between loop calls and repeated STOP while idle;
- changing the target in the same direction and reversing direction;
- stopping when the target equals the current position;
- the toggle sequence, independence of two instances and advertised entity capabilities;
- `millis()` wraparound and 120,000 clock steps without accumulated position error;
- full `manual_control` runs, repeated commands and direction changes;
- skipping STOP at endpoints with `has_built_in_endstop`, intermediate and explicit STOP;
- `start_position: open/closed`: full travel even when the saved position already matches
  the endpoint, without overrun and with STOP even when `has_built_in_endstop` is enabled;
- saving the position after startup travel and restoring it with `asis`;
- deferring the latest target during startup and cancelling it with STOP;
- 5% overrun in both directions, based on the full travel time for each direction;
- overrun alone when an endpoint command is repeated while idle at that endpoint;
- thousands of repeated commands during overrun without postponing STOP;
- interrupting overrun with STOP/a target change, and no overrun for intermediate targets;
- `manual_control` with overrun and `millis()` wraparound during overrun;
- `has_built_in_endstop` behavior after overrun during normal operation.

YAML validation also covers missing required actions/durations, zero,
negative and excessive durations, and an unsupported option. It checks all three
`start_position` values, rejects an invalid value, and validates the percentage range
of `end_positions_overrun` (rejecting negative values, values above 100% and a duration of `5s`).

Testing did not cover a physical actuator, actual relay timing, persistence after
cutting power to the ESP, or display/control in a running Home Assistant instance.
Compilation with other ESPHome versions or for ESP32 has not been verified.
