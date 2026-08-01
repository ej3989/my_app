"""Switch platform for BT Practice BLE."""

from __future__ import annotations

import asyncio
from contextlib import suppress
import logging
from typing import Any

from bleak import BleakClient, BleakError

from homeassistant.components import bluetooth
from homeassistant.components.switch import SwitchEntity
from homeassistant.config_entries import ConfigEntry
from homeassistant.const import CONF_ADDRESS
from homeassistant.core import HomeAssistant, callback
from homeassistant.exceptions import HomeAssistantError
from homeassistant.helpers import device_registry as dr
from homeassistant.helpers.device_registry import DeviceInfo
from homeassistant.helpers.entity_platform import AddConfigEntryEntitiesCallback

from .const import DOMAIN, LED_CONTROL_UUID

_LOGGER = logging.getLogger(__name__)

CONNECT_TIMEOUT = 20.0
COMMAND_TIMEOUT = 30.0
RECONNECT_DELAYS = (1, 2, 4, 8, 15, 30)


async def async_setup_entry(
    hass: HomeAssistant,
    entry: ConfigEntry,
    async_add_entities: AddConfigEntryEntitiesCallback,
) -> None:
    """Set up the BT Practice BLE switch."""
    async_add_entities([BTPracticeLEDSwitch(entry)])


class BTPracticeLEDSwitch(SwitchEntity):
    """Control the BT Practice RGB LED over a persistent BLE connection."""

    _attr_has_entity_name = True
    _attr_name = "LED"
    _attr_should_poll = False

    def __init__(self, entry: ConfigEntry) -> None:
        """Initialize the switch."""
        self._entry = entry
        self._address = entry.data[CONF_ADDRESS]
        self._attr_unique_id = f"{self._address}_led"
        self._attr_is_on = False
        self._attr_available = False
        self._attr_device_info = DeviceInfo(
            identifiers={(DOMAIN, self._address)},
            connections={(dr.CONNECTION_BLUETOOTH, self._address)},
            name=entry.title,
            manufacturer="Zephyr practice",
            model="ESP32-S3 BLE RGB LED",
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
            f"BT Practice BLE connection {self._address}",
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
    def _async_handle_notification(self, state: int) -> None:
        """Apply an LED state received by GATT notification."""
        self._attr_is_on = state == 1
        self._attr_available = True
        self.async_write_ha_state()
        _LOGGER.debug("Received LED state notification: %s", state)

    def _notification_handler(self, _characteristic: Any, data: bytearray) -> None:
        """Receive a notification from Bleak."""
        if not data:
            return

        self.hass.loop.call_soon_threadsafe(
            self._async_handle_notification, data[0]
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
        """Maintain the BLE connection and notification subscription."""
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
                        "BT Practice device %s is not currently advertising",
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
                            LED_CONTROL_UUID, self._notification_handler
                        )
                        value = await client.read_gatt_char(LED_CONTROL_UUID)

                    async with self._operation_lock:
                        self._client = client
                        self._client_ready.set()

                    if value:
                        self._async_handle_notification(value[0])
                    else:
                        self._async_set_available(True)

                    _LOGGER.info(
                        "Connected to BT Practice device %s and subscribed",
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
                        await client.stop_notify(LED_CONTROL_UUID)
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
                "Reconnecting to BT Practice device in %s seconds",
                reconnect_delay,
            )
            await self._async_wait_for_stop(reconnect_delay)

    async def _async_write_state(self, state: bool) -> None:
        """Write an LED state using the persistent GATT connection."""
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
                    LED_CONTROL_UUID,
                    bytes((1 if state else 0,)),
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
                    f"BT Practice LED 제어에 실패했습니다: {err}"
                ) from err

        # The notification normally updates this state first. Updating it here
        # also keeps the entity responsive if a platform delays notifications.
        self._attr_is_on = state
        self._attr_available = True
        self.async_write_ha_state()

    async def async_turn_on(self, **kwargs: Any) -> None:
        """Turn the RGB LED on."""
        await self._async_write_state(True)

    async def async_turn_off(self, **kwargs: Any) -> None:
        """Turn the RGB LED off."""
        await self._async_write_state(False)
