"""Native-API integration test: a simulated HA peer drives real host firmware."""
import asyncio
import os
from pathlib import Path
import subprocess
import tempfile

from aioesphomeapi import APIClient, APIConnectionError

ROOT = Path(__file__).resolve().parents[2]
BINARY = ROOT / 'tests/mixing_valve_climate/.esphome/build/mixing-ha-import-test/.pioenvs/mixing-ha-import-test/program'


async def exercise():
    source = None  # Missing entity: HA sends no response.
    state = {}
    counts = {'subscriptions': 0, 'requests': 0}
    client = None
    logs_client = None

    async def connect():
        nonlocal client
        client = APIClient('127.0.0.1', 6058, '')
        await client.connect(login=True)
        entities, _ = await client.list_entities_services()
        names = {entity.key: entity.name for entity in entities}
        client.subscribe_states(lambda message: state.update({names.get(message.key): message.state})
                                if hasattr(message, 'state') else None)

        def reply(entity_id, attribute):
            assert entity_id == 'binary_sensor.test_boiler'
            if source is not None:
                client.send_home_assistant_state(entity_id, attribute, source)

        def subscribed(entity_id, attribute):
            counts['subscriptions'] += 1
            reply(entity_id, attribute)

        def requested(entity_id, attribute):
            counts['requests'] += 1
            reply(entity_id, attribute)

        client.subscribe_home_assistant_states(subscribed, requested)

    async def expect(value, timeout=5):
        deadline = asyncio.get_running_loop().time() + timeout
        while asyncio.get_running_loop().time() < deadline:
            if state.get('Controller status') == value:
                return
            await asyncio.sleep(0.025)
        raise AssertionError((value, state, counts))

    try:
        for attempt in range(60):
            try:
                await connect()
                break
            except (OSError, APIConnectionError):
                if client is not None:
                    await client.disconnect()
                if attempt == 59:
                    raise
                await asyncio.sleep(0.05)
        await expect('opening')
        # A formerly absent entity is discovered by a current-state request.
        source = 'off'
        await expect('waiting_for_heat')
        before = counts['requests']
        await asyncio.sleep(3.5)  # Beyond timeout, but unchanged replies refresh validity.
        assert state['Controller status'] == 'waiting_for_heat', state
        assert counts['requests'] >= before + 5, counts
        assert counts['subscriptions'] == 1, counts
        assert state['Subscription count'] == 1, state  # No growing callback vector.
        source = None  # Entity deleted while the API connection stays healthy.
        await expect('opening')
        source = 'off'
        await expect('waiting_for_heat')
        for value in ('unknown', 'unavailable', 'unexpected'):
            source = value
            client.send_home_assistant_state('binary_sensor.test_boiler', None, value)
            await expect('opening')
            source = 'off'
            await expect('waiting_for_heat')
        source = 'on'
        await expect('opening')
        source = 'off'
        await expect('waiting_for_heat')
        logs_client = APIClient('127.0.0.1', 6058, '')
        await logs_client.connect(login=True)
        logs_client.subscribe_logs(lambda message: None)
        await client.disconnect()
        await asyncio.sleep(1.3)  # Disconnection must clear the former off state.
        source = None
        state.clear()
        await connect()
        await expect('opening')
        source = 'off'
        await expect('waiting_for_heat')
        source = 'on'
        await expect('opening')
        assert state['Subscription count'] == 1, state
        print('PASS: native API import, unchanged replies, deletion, unavailable, disconnect/reconnect, bounded subscriptions')
    finally:
        if client is not None:
            await client.disconnect()
        if logs_client is not None:
            await logs_client.disconnect()


with tempfile.TemporaryDirectory(prefix='mixing-ha-prefs-') as prefs:
    with tempfile.TemporaryFile() as log:
        process = subprocess.Popen([str(BINARY)], stdout=log, stderr=subprocess.STDOUT,
                                   env={**os.environ, 'ESPHOME_PREFDIR': prefs})
        try:
            asyncio.run(asyncio.wait_for(exercise(), timeout=30))
        except BaseException:
            log.seek(0)
            print(log.read().decode(errors='replace'))
            raise
        finally:
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
