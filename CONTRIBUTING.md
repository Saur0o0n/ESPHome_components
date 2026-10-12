# Adding components

Each component has its own `snake_case` name and separate directories:

- `components/<name>/` — ESPHome source, including `__init__.py` and the platform module;
- `docs/<name>/README.md` — installation, options, behavior and limitations;
- `examples/<name>/` — example YAML files without real credentials;
- `tests/<name>/run.py` — entry point for the component's checks.

1. Add the component to the table in the root README.
2. In examples, use the local source `../../components` and an explicit
   `components: [<name>]` list. The path is relative to the main YAML file.
3. Add behavioral tests appropriate to the component and configuration validation.
   The root `tests/run.py` automatically discovers subdirectories with their own `run.py`.
4. State the tested ESPHome version and distinguish compilation, software tests
   and testing on physical hardware.
5. Preserve the provenance and license information of any adapted code.
6. Write documentation, comments and example labels in English.

Do not add `secrets.yaml`, private device configurations, firmware binaries,
caches or build directories to the repository. Examples should use clearly
fictional credentials or references to the user's secrets.

Run checks from the repository root:

```sh
python tests/run.py
esphome compile examples/time_based_valve/esp8266.yaml
```

The existing valve component is tested with ESPHome 2026.8.2.
