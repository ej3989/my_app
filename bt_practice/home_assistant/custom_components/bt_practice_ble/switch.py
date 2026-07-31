"""Switch platform for BT Practice BLE."""

from __future__ import annotations

import asyncio
from datetime import timedelta
import logging
from typing import Any

from bleak import BleakClient, BleakError

from homeassistant.components import bluetooth
from homeassistant.components.switch import SwitchEntity
from homeassistant.config_entries import ConfigEntry
from homeassistant.const import CONF_ADDRESS
from homeassistant.core import HomeAssistant
from homeassistant.exceptions import HomeAssistantError
from homeassistant.helpers import device_registry as dr
from homeassistant.helpers.device_registry import DeviceInfo
from homeassistant.helpers.entity_platform import AddConfigEntryEntitiesCallback

from .const import DOMAIN, LED_CONTROL_UUID

_LOGGER = logging.getLogger(__name__)

SCAN_INTERVAL = timedelta(seconds=60)
CONNECT_TIMEOUT = 20.0


async def async_setup_entry(
    hass: HomeAssistant,
    entry: ConfigEntry,
    async_add_entities: AddConfigEntryEntitiesCallback,
) -> None:
    """Set up the BT Practice BLE switch."""
    async_add_entities(
        [BTPracticeLEDSwitch(entry.data[CONF_ADDRESS], entry.title)]
    )


class BTPracticeLEDSwitch(SwitchEntity):
    """Control the BT Practice RGB LED over Bluetooth LE."""

    _attr_has_entity_name = True
    _attr_name = "LED"

    def __init__(self, address: str, device_name: str) -> None:
        """Initialize the switch."""
        self._address = address
        self._attr_unique_id = f"{address}_led"
        self._attr_is_on = False
        self._attr_available = True
        self._operation_lock = asyncio.Lock()
        self._attr_device_info = DeviceInfo(
            identifiers={(DOMAIN, address)},
            connections={(dr.CONNECTION_BLUETOOTH, address)},
            name=device_name,
            manufacturer="Zephyr practice",
            model="ESP32-S3 BLE RGB LED",
        )

    async def _async_get_client(self) -> BleakClient:
        """Create a client for the nearest connectable Bluetooth adapter."""
        ble_device = bluetooth.async_ble_device_from_address(
            self.hass, self._address, connectable=True
        )
        if ble_device is None:
            raise HomeAssistantError(
                "BT Practice 장치를 찾을 수 없습니다. 장치가 광고 중인지 확인하세요."
            )

        return BleakClient(ble_device, timeout=CONNECT_TIMEOUT)

    async def _async_write_state(self, state: bool) -> None:
        """Write an LED state to the GATT characteristic."""
        async with self._operation_lock:
            client = await self._async_get_client()
            try:
                async with client:
                    await client.write_gatt_char(
                        LED_CONTROL_UUID,
                        bytes((1 if state else 0,)),
                        response=True,
                    )
            except (BleakError, TimeoutError) as err:
                self._attr_available = False
                self.async_write_ha_state()
                raise HomeAssistantError(
                    f"BT Practice LED 제어에 실패했습니다: {err}"
                ) from err

        self._attr_is_on = state
        self._attr_available = True
        self.async_write_ha_state()

    async def async_turn_on(self, **kwargs: Any) -> None:
        """Turn the RGB LED on."""
        await self._async_write_state(True)

    async def async_turn_off(self, **kwargs: Any) -> None:
        """Turn the RGB LED off."""
        await self._async_write_state(False)

    async def async_update(self) -> None:
        """Read the current LED state over GATT."""
        async with self._operation_lock:
            try:
                client = await self._async_get_client()
                async with client:
                    value = await client.read_gatt_char(LED_CONTROL_UUID)
            except (BleakError, HomeAssistantError, TimeoutError) as err:
                self._attr_available = False
                _LOGGER.debug("Unable to update BT Practice LED: %s", err)
                return

        if value:
            self._attr_is_on = value[0] == 1
        self._attr_available = True

