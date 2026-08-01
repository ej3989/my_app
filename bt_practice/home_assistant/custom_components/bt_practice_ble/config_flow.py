"""Config flow for BT Practice BLE."""

from __future__ import annotations

from typing import Any

import voluptuous as vol

from homeassistant.components.bluetooth import (
    BluetoothServiceInfoBleak,
    async_discovered_service_info,
)
from homeassistant.config_entries import ConfigFlow, ConfigFlowResult
from homeassistant.const import CONF_ADDRESS

from .const import DOMAIN, FAN_SERVICE_UUID


class BTPracticeBLEConfigFlow(ConfigFlow, domain=DOMAIN):
    """Handle a config flow for BT Practice BLE."""

    VERSION = 1

    def __init__(self) -> None:
        """Initialize the config flow."""
        self._discovery_info: BluetoothServiceInfoBleak | None = None
        self._discovered_devices: dict[str, BluetoothServiceInfoBleak] = {}

    async def async_step_bluetooth(
        self, discovery_info: BluetoothServiceInfoBleak
    ) -> ConfigFlowResult:
        """Handle discovery by the Home Assistant Bluetooth integration."""
        await self.async_set_unique_id(discovery_info.address)
        self._abort_if_unique_id_configured()

        self._discovery_info = discovery_info
        self.context["title_placeholders"] = {
            "name": discovery_info.name or "BT Practice"
        }
        return await self.async_step_confirm()

    async def async_step_confirm(
        self, user_input: dict[str, Any] | None = None
    ) -> ConfigFlowResult:
        """Ask the user to confirm a discovered device."""
        if self._discovery_info is None:
            return self.async_abort(reason="no_devices_found")

        if user_input is not None:
            return self.async_create_entry(
                title=self._discovery_info.name or "BT Practice",
                data={CONF_ADDRESS: self._discovery_info.address},
            )

        self.context["title_placeholders"] = {
            "name": self._discovery_info.name or "BT Practice"
        }
        return self.async_show_form(step_id="confirm")

    async def async_step_user(
        self, user_input: dict[str, Any] | None = None
    ) -> ConfigFlowResult:
        """Allow manual selection from discovered BT Practice devices."""
        if user_input is not None:
            address = user_input[CONF_ADDRESS]
            discovery_info = self._discovered_devices[address]

            await self.async_set_unique_id(address, raise_on_progress=False)
            self._abort_if_unique_id_configured()

            return self.async_create_entry(
                title=discovery_info.name or "BT Practice",
                data={CONF_ADDRESS: address},
            )

        current_addresses = self._async_current_ids(include_ignore=False)

        for discovery_info in async_discovered_service_info(self.hass):
            service_uuids = {
                service_uuid.lower()
                for service_uuid in discovery_info.service_uuids
            }
            if (
                discovery_info.address in current_addresses
                or discovery_info.address in self._discovered_devices
                or FAN_SERVICE_UUID not in service_uuids
                or not discovery_info.connectable
            ):
                continue

            self._discovered_devices[discovery_info.address] = discovery_info

        if not self._discovered_devices:
            return self.async_abort(reason="no_devices_found")

        return self.async_show_form(
            step_id="user",
            data_schema=vol.Schema(
                {
                    vol.Required(CONF_ADDRESS): vol.In(
                        {
                            address: (
                                f"{info.name or 'BT Practice'} ({address})"
                            )
                            for address, info in self._discovered_devices.items()
                        }
                    )
                }
            ),
        )
