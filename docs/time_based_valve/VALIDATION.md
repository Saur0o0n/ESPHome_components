# Validation — 2026-10-12

Environment: local `esphome-builder` container, ESPHome **2026.8.2**.
Sources were copied to a temporary directory inside the container for testing.
No existing device configurations were changed and no firmware was flashed.

| Check | Result |
|---|---|
| `esphome config examples/time_based_valve/esp8266.yaml` | PASS |
| `python tests/run.py`: configuration validation | PASS — 13 test methods, including subcases |
| `python tests/run.py`: host compilation | PASS — platformio/native 1.2.1 |
| `python tests/run.py`: C++ behavioral tests | PASS |
| YAML-generated standard valve actions and custom control (literal/templated) | PASS |
| Pulsed movement C++ behavioral tests | PASS |
| `esphome compile examples/time_based_valve/esp8266.yaml` | PASS — nodemcuv2, Arduino 3.1.2 |
| `esphome compile examples/time_based_valve/esp32.yaml` | PASS — esp32dev, ESP-IDF 5.5.5 |

The ESP8266 and ESP32 builds include two valves, four interlocked GPIO switches,
Wi-Fi, the native API, pulsed/continuous control buttons and a templated position
action driven by a number entity. Both valves use `start_position: closed` and
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
Compilation with other ESPHome versions has not been verified.

## Pulsed movement coverage

The existing continuous/startup/overrun regression suite still passes. Additional
controlled-clock checks cover:

- 20% to 25% with three 2 s opening pulses and two 8 s pauses (22 s total);
- separate opening/closing pulse, pause and full travel durations;
- unchanged position and OPENING/CLOSING operation during pauses;
- STOP and toggle during pulses/pauses, with no later restart;
- repeated targets, including manual control, without shifting phase deadlines;
- updated/equal targets, same-direction changes and reversals during a pause;
- immediate continuous takeover without briefly restarting the old direction;
- conversions in both directions between continuous and pulsed movement;
- uninterrupted endpoint overrun, repeat suppression and preserved remaining time
  when movement mode changes, in both opening and closing directions;
- built-in endstop behavior and explicit release of its output;
- continuous startup with queued target/movement, latest-command selection and STOP;
- clock wraparound within active pulses and within pauses;
- late loops accounting for actual powered time without replaying missed pulses;
- 120,000 short pulses without accumulated floating-point position drift;
- no preferences writes at pulse pauses, saving at STOP, and no pulse resumption on reboot;
- invalid dynamic positions/modes and independent instances.

Schema tests additionally reject invalid pulse/pause durations and invalid/missing
action fields. Host code generation exercises fixed and templated movement modes,
positions and the default continuous action mode. Both MCU examples compile an
action with a templated position using a trigger argument.

Outputs in the host tests are simulated. Physical relay timing, interlock delays,
actuator response and Home Assistant presentation require hardware validation.

## Direction change wait — 2026-10-13

ESPHome 2026.8.2: all 14 valve schema tests (including subcases), generated YAML
actions and the real-runtime host suite pass. The climate schema, host behavioral
suite and HA template tests also pass unchanged. New controlled-clock checks cover:

- both reversal directions: outputs off and position unchanged for 200 ms,
  followed by a full 2 s powered pulse;
- no delay when starting from idle, immediately after explicit STOP, from a pulse
  pause, on ordinary pulse resumption, or on a same-direction target change;
- STOP/toggle cancellation with no delayed restart;
- repeated targets, movement-mode changes and replacement directions during a wait;
- late loops starting a complete pulse at actual action dispatch;
- continuous partial targets and full endpoint travel/overrun excluding wait time;
- timer wraparound, energized outputs at built-in endstops and immediate full startup travel.

The existing zero-delay regression suite continues to exercise default behavior.
Hardware switching delays and physical actuator movement have not been measured.
