import types

import pytest

from screamrouter.configuration.configuration_manager import ConfigurationManager
from screamrouter.screamrouter_types.configuration import (
    RouteDescription,
    SinkDescription,
    SourceDescription,
)
from screamrouter.screamrouter_types.exceptions import InUseError


def _make_manager() -> ConfigurationManager:
    manager = ConfigurationManager.__new__(ConfigurationManager)
    manager.sink_descriptions = []
    manager.active_temporary_sinks = []
    manager.source_descriptions = []
    manager.route_descriptions = []
    manager.active_temporary_routes = []
    manager.discovered_devices = {}
    manager.temp_entity_manager = None
    manager.active_configuration = None
    manager.configuration_semaphore = None
    manager.reload_condition = None
    manager.reload_config = False
    manager.volume_eq_reload_condition = None
    manager.running = False
    manager.mdns_responder = None
    manager.mdns_pinger = None
    manager.mdns_settings_pinger = None
    manager.mdns_scream_advertiser = None
    manager._ConfigurationManager__api_webstream = None
    manager._ConfigurationManager__reload_configuration = types.MethodType(lambda self: None, manager)
    manager._ConfigurationManager__reload_configuration_without_save = types.MethodType(lambda self: None, manager)
    return manager


def test_delete_sink_removes_group_membership():
    manager = _make_manager()

    sink = SinkDescription(name="sink-a")
    group = SinkDescription(name="group-1", is_group=True, group_members=["sink-a"])
    parent_group = SinkDescription(name="group-parent", is_group=True, group_members=["group-1"])

    manager.sink_descriptions = [sink, group, parent_group]

    assert manager.delete_sink("sink-a") is True
    assert sink not in manager.sink_descriptions
    assert group.group_members == []
    assert parent_group.group_members == ["group-1"]


def test_delete_sink_restores_groups_when_in_use():
    manager = _make_manager()

    sink = SinkDescription(name="sink-a")
    group = SinkDescription(name="group-1", is_group=True, group_members=["sink-a"])
    manager.sink_descriptions = [sink, group]

    manager.source_descriptions = [SourceDescription(name="source-a")]
    manager.route_descriptions = [
        RouteDescription(name="route-a", sink="sink-a", source="source-a"),
    ]

    with pytest.raises(InUseError):
        manager.delete_sink("sink-a")

    assert group.group_members == ["sink-a"]
    assert sink in manager.sink_descriptions


def test_delete_source_removes_group_membership():
    manager = _make_manager()

    source = SourceDescription(name="source-a")
    group = SourceDescription(name="source-group", is_group=True, group_members=["source-a"])

    manager.source_descriptions = [source, group]

    assert manager.delete_source("source-a") is True
    assert source not in manager.source_descriptions
    assert group.group_members == []


def test_delete_source_restores_groups_when_in_use():
    manager = _make_manager()

    source = SourceDescription(name="source-a")
    group = SourceDescription(name="source-group", is_group=True, group_members=["source-a"])
    manager.source_descriptions = [source, group]

    manager.sink_descriptions = [SinkDescription(name="sink-a")]
    manager.route_descriptions = [
        RouteDescription(name="route-a", sink="sink-a", source="source-a"),
    ]

    with pytest.raises(InUseError):
        manager.delete_source("source-a")

    assert group.group_members == ["source-a"]
    assert source in manager.source_descriptions
