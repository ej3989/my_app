"""Fan platform for BT Practice BLE."""

from __future__ import annotations

import asyncio
from contextlib import suppress
import logging
import math
from typing import Any

from bleak import BleakClient, BleakError

from homeassistant.components import bluetooth
from homeassistant.components.fan import FanEntity, FanEntityFeature
from homeassistant.config_entries import ConfigEntry
from homeassistant.const import CONF_ADDRESS
from homeassistant.core import HomeAssistant, callback
from homeassistant.exceptions import HomeAssistantError
from homeassistant.helpers import device_registry as dr
from homeassistant.helpers.device_registry import DeviceInfo
from homeassistant.helpers.entity_platform import AddConfigEntryEntitiesCallback

from .const import DOMAIN, FAN_COMMAND_UUID, FAN_STATE_UUID

_LOGGER = logging.getLogger(__name__)

CONNECT_TIMEOUT = 20.0
COMMAND_TIMEOUT = 30.0
RECONNECT_DELAYS = (1, 2, 4, 8, 15, 30)

COMMAND_SET_SPEED = 0x01
COMMAND_SET_OSCILLATION = 0x02
COMMAND_POWER_OFF = 0x03

SPEED_COUNT = 3
SPEED_PERCENTAGES = (0, 33, 67, 100)


async def async_setup_entry(
    hass: HomeAssistant,
    entry: ConfigEntry,
    async_add_entities: AddConfigEntryEntitiesCallback,
) -> None:
    """Set up the BT Practice BLE fan."""
    async_add_entities([BTPracticeFan(entry)])


class BTPracticeFan(FanEntity):
    """Control a three-speed mechanical fan over persistent BLE."""

    _attr_has_entity_name = True
    _attr_name = None
    _attr_should_poll = False
    _attr_speed_count = SPEED_COUNT
    _attr_supported_features = (
        FanEntityFeature.SET_SPEED
        | FanEntityFeature.OSCILLATE
        | FanEntityFeature.TURN_ON
        | FanEntityFeature.TURN_OFF
    )

    def __init__(self, entry: ConfigEntry) -> None:
        """Initialize the fan entity."""
        self._entry = entry
        self._address = entry.data[CONF_ADDRESS]
        self._attr_unique_id = f"{self._address}_fan"
        self._attr_is_on = False
        self._attr_percentage = 0
        self._attr_oscillating = False
        self._attr_available = False
        self._last_nonzero_speed = 1
        self._attr_device_info = DeviceInfo(
            identifiers={(DOMAIN, self._address)},
            connections={(dr.CONNECTION_BLUETOOTH, self._address)},
            name=entry.title,
            manufacturer="Zephyr practice",
            model="ESP32-S3 BLE relay fan controller",
        )

        self._client: BleakClient | None = None
        self._client_ready = asyncio.Event()
        self._stop_event = asyncio.Event()
        self._operation_lock = asyncio.Lock()
        self._connection_task: asyncio.Task[None] | None = None

    async def async_added_to_hass(self) -> None:
        """Start the persistent BLE connection manager."""
        await super().async_added_to_hass()
        self._connection_task = self._entry.async_create_background_task(
            self.hass,
            self._async_connection_loop(),
            f"BT Practice BLE fan connection {self._address}",
        )

    async def async_will_remove_from_hass(self) -> None:
        """Stop the connection manager and disconnect cleanly."""
        self._stop_event.set()

        if self._connection_task is not None:
            with suppress(asyncio.CancelledError):
                await self._connection_task

        await super().async_will_remove_from_hass()

    def _async_ble_device(self):
        """Return the nearest connectable representation of the device."""
        return bluetooth.async_ble_device_from_address(
            self.hass, self._address, connectable=True
        )

    @callback
    def _async_set_available(self, available: bool) -> None:
        """Update entity availability from the connection manager."""
        if self._attr_available == available:
            return

        self._attr_available = available
        self.async_write_ha_state()

    @callback
    def _async_handle_notification(self, data: bytes | bytearray) -> None:
        """Apply speed and oscillation received from the board."""
        if len(data) < 2 or data[0] > SPEED_COUNT or data[1] > 1:
            _LOGGER.warning("Ignored invalid fan state packet: %s", data.hex())
            return

        speed = data[0]
        if speed > 0:
            self._last_nonzero_speed = speed

        self._attr_is_on = speed > 0
        self._attr_percentage = SPEED_PERCENTAGES[speed]
        self._attr_oscillating = data[1] == 1
        self._attr_available = True
        self.async_write_ha_state()
        _LOGGER.debug(
            "Received fan state: speed=%s oscillation=%s",
            speed,
            self._attr_oscillating,
        )

    def _notification_handler(self, _characteristic: Any, data: bytearray) -> None:
        """Receive a notification from Bleak."""
        self.hass.loop.call_soon_threadsafe(
            self._async_handle_notification, bytes(data)
        )

    async def _async_wait_for_stop(self, delay: int) -> None:
        """Wait for a reconnect delay, waking early during shutdown."""
        try:
            async with asyncio.timeout(delay):
                await self._stop_event.wait()
        except TimeoutError:
            pass

    async def _async_wait_until_disconnected(
        self, disconnected_event: asyncio.Event
    ) -> None:
        """Wait until either the BLE link drops or the entity is removed."""
        disconnected_task = asyncio.create_task(disconnected_event.wait())
        stop_task = asyncio.create_task(self._stop_event.wait())

        done, pending = await asyncio.wait(
            {disconnected_task, stop_task},
            return_when=asyncio.FIRST_COMPLETED,
        )

        for task in pending:
            task.cancel()
        await asyncio.gather(*pending, return_exceptions=True)

        for task in done:
            task.result()

    async def _async_connection_loop(self) -> None:
        """Maintain the BLE connection and state notification subscription."""
        reconnect_index = 0

        while not self._stop_event.is_set():
            client: BleakClient | None = None
            disconnected_event = asyncio.Event()

            def disconnected_callback(_client: BleakClient) -> None:
                self.hass.loop.call_soon_threadsafe(disconnected_event.set)

            try:
                ble_device = self._async_ble_device()
                if ble_device is None:
                    _LOGGER.debug(
                        "BT Practice fan %s is not currently advertising",
                        self._address,
                    )
                else:
                    client = BleakClient(
                        ble_device,
                        timeout=CONNECT_TIMEOUT,
                        disconnected_callback=disconnected_callback,
                    )

                    async with asyncio.timeout(CONNECT_TIMEOUT):
                        await client.connect()
                        await client.start_notify(
                            FAN_STATE_UUID, self._notification_handler
                        )
                        value = await client.read_gatt_char(FAN_STATE_UUID)

                    async with self._operation_lock:
                        self._client = client
                        self._client_ready.set()

                    if value:
                        self._async_handle_notification(value)
                    else:
                        self._async_set_available(True)

                    _LOGGER.info(
                        "Connected to BT Practice fan %s and subscribed",
                        self._address,
                    )
                    reconnect_index = 0

                    await self._async_wait_until_disconnected(
                        disconnected_event
                    )

            except (BleakError, TimeoutError) as err:
                _LOGGER.warning(
                    "BT Practice BLE connection failed for %s: %s",
                    self._address,
                    err,
                )
            finally:
                async with self._operation_lock:
                    self._client_ready.clear()
                    if self._client is client:
                        self._client = None

                self._async_set_available(False)

                if client is not None and client.is_connected:
                    try:
                        await client.stop_notify(FAN_STATE_UUID)
                    except (BleakError, TimeoutError):
                        pass

                    try:
                        await client.disconnect()
                    except (BleakError, TimeoutError):
                        pass

            if self._stop_event.is_set():
                break

            reconnect_delay = RECONNECT_DELAYS[reconnect_index]
            reconnect_index = min(
                reconnect_index + 1, len(RECONNECT_DELAYS) - 1
            )
            _LOGGER.debug(
                "Reconnecting to BT Practice fan in %s seconds",
                reconnect_delay,
            )
            await self._async_wait_for_stop(reconnect_delay)

    async def _async_write_command(self, command: int, value: int) -> None:
        """Write a two-byte command over the persistent GATT connection."""
        try:
            async with asyncio.timeout(COMMAND_TIMEOUT):
                await self._client_ready.wait()
        except TimeoutError as err:
            raise HomeAssistantError(
                "BT Practice 장치에 연결할 수 없어 명령 시간이 초과됐습니다."
            ) from err

        async with self._operation_lock:
            client = self._client
            if client is None or not client.is_connected:
                raise HomeAssistantError(
                    "BT Practice BLE 연결이 끊어졌습니다. 재연결 후 다시 시도하세요."
                )

            try:
                await client.write_gatt_char(
                    FAN_COMMAND_UUID,
                    bytes((command, value)),
                    response=True,
                )
            except (BleakError, TimeoutError) as err:
                self._client_ready.clear()
                self._async_set_available(False)
                try:
                    await client.disconnect()
                except (BleakError, TimeoutError):
                    pass
                raise HomeAssistantError(
                    f"BT Practice 선풍기 제어에 실패했습니다: {err}"
                ) from err

    async def async_turn_on(
        self,
        percentage: int | None = None,
        preset_mode: str | None = None,
        **kwargs: Any,
    ) -> None:
        """Turn on at the requested or most recently used speed."""
        del preset_mode, kwargs
        if percentage is None:
            speed = self._last_nonzero_speed
        else:
            speed = self._speed_from_percentage(percentage)
            if speed == 0:
                speed = 1

        await self._async_write_command(COMMAND_SET_SPEED, speed)
        self._last_nonzero_speed = speed
        self._attr_is_on = True
        self._attr_percentage = SPEED_PERCENTAGES[speed]
        self.async_write_ha_state()

    async def async_turn_off(self, **kwargs: Any) -> None:
        """Turn off all speed relays and the oscillation relay."""
        del kwargs
        await self._async_write_command(COMMAND_POWER_OFF, 0)
        self._attr_is_on = False
        self._attr_percentage = 0
        self._attr_oscillating = False
        self.async_write_ha_state()

    async def async_set_percentage(self, percentage: int) -> None:
        """Select one of the three mutually exclusive speed relays."""
        speed = self._speed_from_percentage(percentage)
        await self._async_write_command(COMMAND_SET_SPEED, speed)

        if speed > 0:
            self._last_nonzero_speed = speed
        self._attr_is_on = speed > 0
        self._attr_percentage = SPEED_PERCENTAGES[speed]
        self.async_write_ha_state()

    async def async_oscillate(self, oscillating: bool) -> None:
        """Hold the active-high oscillation relay on or off."""
        await self._async_write_command(
            COMMAND_SET_OSCILLATION, 1 if oscillating else 0
        )
        self._attr_oscillating = oscillating
        self.async_write_ha_state()

    @staticmethod
    def _speed_from_percentage(percentage: int) -> int:
        """Map Home Assistant's percentage to relay speed 0..3."""
        if percentage <= 0:
            return 0
        return min(SPEED_COUNT, math.ceil(percentage * SPEED_COUNT / 100))
