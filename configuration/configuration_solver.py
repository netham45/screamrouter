"""Contains the logic to solve a configuration into sinks to sources with volume and equalization"""
from copy import copy
from typing import List, Optional
from screamrouter_types import DelayType, SourceNameType, SinkNameType, RouteNameType
from screamrouter_types import SourceDescription, SinkDescription, RouteDescription
from screamrouter_types import Equalizer, VolumeType


#def unique[T](list: List[T]) -> List[T]:  # One day
def unique(list_in: List) -> List:
    """Returns a list with duplicates filtered out"""
    return_list = []
    for element in list_in:
        if not element in return_list:
            return_list.append(element)
    return return_list

class ConfigurationSolver():
    """This logic converts a configuration into a one to many map of Sinks to Sources
       During the recursive lookup audio, equalizer levels, and delay levels are
       adjusted per the settings the user has entered."""
    def __init__(self, sources: List[SourceDescription],
                 sinks: List[SinkDescription],
                 routes: List[RouteDescription]) -> None:
        self.sources: List[SourceDescription] = sources
        self.sinks: List[SinkDescription] = sinks
        self.routes: List[RouteDescription] = routes

        # Build Real Sink Names to Sources
        self.real_sinks_to_real_sources: dict[SinkDescription, List[SourceDescription]]
        self.real_sinks_to_real_sources = self.__get_real_sinks_to_real_sources()

    def __get_real_sinks_to_real_sources(self):
        """Returns a list of real sinks with all active sources on the sink"""
        # Get all real sinks by route
        routes_to_real_sinks: dict[RouteDescription, List[SinkDescription]]
        routes_to_real_sinks = self.__get_routes_to_real_sinks()

        # Get all real sources by route
        routes_to_real_sources: dict[RouteDescription, List[SourceDescription]]
        routes_to_real_sources = self.__get_routes_to_real_sources()

        real_sinks_to_real_sources: dict[SinkDescription, List[SourceDescription]] = {}

        # Combine them into a big table
        for route_name, real_sinks in routes_to_real_sinks.items():
            for real_sink in real_sinks:
                for real_source in routes_to_real_sources[route_name]:
                    real_source_copy: SourceDescription = copy(real_source)
                    real_source_copy.volume = real_source.volume * real_sink.volume
                    real_source_copy.equalizer = real_source.equalizer * real_sink.equalizer
                    real_source_copy.delay = real_source.delay + real_sink.delay
                    sink_to_append_to: List[SinkDescription]
                    sink_to_append_to = [sink for sink in real_sinks_to_real_sources
                                              if sink.name == real_sink.name]
                    if len(sink_to_append_to) == 0:
                        if not real_sink in real_sinks_to_real_sources:
                            real_sinks_to_real_sources[real_sink] = []
                        real_sinks_to_real_sources[real_sink].append(real_source_copy)
                    else:
                        real_sinks_to_real_sources[sink_to_append_to[0]].append(real_source_copy)
        return real_sinks_to_real_sources

    def __get_routes_to_real_sources(self) -> dict[RouteDescription, List[SourceDescription]]:
        """Returns a map of routes to real enabled sources
           that has been adjusted for volume and equalization"""
        routes_to_real_sources: dict[RouteDescription, List[SourceDescription]] = {}
        for route in self.routes:
            if route.enabled:
                source: SourceDescription = self.get_source_from_name(route.source)
                routes_to_real_sources[route] = self.get_real_sources_from_source(
                                                                                  source,
                                                                                  True,
                                                                                  route.volume,
                                                                                  route.equalizer,
                                                                                  route.delay)
        return routes_to_real_sources

    def __get_routes_to_real_sinks(self) -> dict[RouteDescription, List[SinkDescription]]:
        """Returns a map of routes to real enabled sinks
           that has been adjusted for volume and equalization"""
        routes_to_real_sinks: dict[RouteDescription, List[SinkDescription]] = {}
        for route in self.routes:
            if route.enabled:
                sink: SinkDescription = self.get_sink_from_name(route.sink)
                routes_to_real_sinks[route] = self.get_real_sinks_from_sink(sink, True)
        return routes_to_real_sinks

    def get_real_sinks_from_sink(self, sink: SinkDescription,
                                   active_only: bool,
                                   volume_adjustment: Optional[VolumeType] = None,
                                   equalizer_adjustment: Optional[Equalizer] = None,
                                   delay_adjustment: Optional[DelayType] = None,
                                   return_groups: bool = False
                                   ) -> List[SinkDescription]:
        """Resolves a sink group and returns a list of all sinks"""
        if volume_adjustment is None:
            volume_adjustment = 1
        if equalizer_adjustment is None:
            equalizer_adjustment = Equalizer(b1=1,b2=1,b3=1,b4=1,b5=1,b6=1,
                                             b7=1,b8=1,b9=1,b10=1,b11=1,b12=1,
                                             b13=1,b14=1,b15=1,b16=1,b17=1,b18=1)
        if delay_adjustment is None:
            delay_adjustment = 0
        adjusted_volume: VolumeType  = volume_adjustment * sink.volume
        adjusted_equalizer: Equalizer = equalizer_adjustment * sink.equalizer
        adjusted_delay: DelayType = delay_adjustment + sink.delay


        if active_only and not sink.enabled:
            return []

        if not sink.is_group:
            sink_copy: SinkDescription = copy(sink)
            sink_copy.volume = adjusted_volume
            sink_copy.equalizer = adjusted_equalizer
            sink_copy.delay = adjusted_delay
            return [sink_copy]

        # Sink is a group, get all group members
        group_members: List[SinkDescription] = []
        for group_member_name in sink.group_members:
            group_member: SinkDescription = self.get_sink_from_name(group_member_name)
            group_members.append(group_member)

        combined_group_members: List[SinkDescription] = []
        for group_member in group_members:
            combined_group_members.extend(self.get_real_sinks_from_sink(group_member,
                                                                              active_only,
                                                                              adjusted_volume,
                                                                              adjusted_equalizer,
                                                                              adjusted_delay,
                                                                              return_groups))
        if return_groups:
            combined_group_members.append(sink)
        return combined_group_members

    def get_real_sources_from_source(self, source: SourceDescription,
                                   active_only: bool,
                                   volume_adjustment: Optional[VolumeType] = None,
                                   equalizer_adjustment: Optional[Equalizer] = None,
                                   delay_adjustment: Optional[DelayType] = None,
                                   return_groups: bool = False
                                   ) -> List[SourceDescription]:
        """Resolves a source group and returns a list of all sources"""
        if volume_adjustment is None:
            volume_adjustment = 1
        if equalizer_adjustment is None:
            equalizer_adjustment = Equalizer(b1=1,b2=1,b3=1,b4=1,b5=1,b6=1,
                                             b7=1,b8=1,b9=1,b10=1,b11=1,b12=1,
                                             b13=1,b14=1,b15=1,b16=1,b17=1,b18=1)
        if delay_adjustment is None:
            delay_adjustment = 0
        adjusted_volume: VolumeType  = volume_adjustment * source.volume
        adjusted_equalizer: Equalizer = equalizer_adjustment * source.equalizer
        adjusted_delay: DelayType = delay_adjustment + source.delay


        if active_only and not source.enabled:
            return []

        if not source.is_group:
            source_copy: SourceDescription = copy(source)
            source_copy.volume = adjusted_volume
            source_copy.equalizer = adjusted_equalizer
            source_copy.delay = adjusted_delay
            return [source_copy]

        # Source is a group, get all group members
        group_members: List[SourceDescription] = []
        for group_member_name in source.group_members:
            group_member: SourceDescription = self.get_source_from_name(group_member_name)
            group_members.append(group_member)

        combined_group_members: List[SourceDescription] = []
        for group_member in group_members:
            combined_group_members.extend(self.get_real_sources_from_source(group_member,
                                                                              active_only,
                                                                              adjusted_volume,
                                                                              adjusted_equalizer,
                                                                              adjusted_delay,
                                                                              return_groups))
        if return_groups:
            combined_group_members.append(source)
        return combined_group_members

    def get_routes_by_sink(self, sink: SinkDescription) -> List[RouteDescription]:
        """Get all routes that use this sink
           Volume levels for the returned routes will be adjusted based off the sink levels
        """
        _routes: List[RouteDescription] = []
        sinks: List[SinkDescription] = self.get_real_sinks_from_sink(sink,
                                                                     False,
                                                                     return_groups=True)
        sinks.append(sink)
        for route in self.routes:
            for _sink in sinks:
                if _sink.name == route.sink:
                    _routes.append(copy(route))
        return unique(_routes)

    def get_routes_by_source(self, source: SourceDescription) -> List[RouteDescription]:
        """Get all routes that use this source
           Volume levels for the returned routes will be adjusted based off the source levels
        """
        _routes: List[RouteDescription] = []
        sources: List[SourceDescription] = self.get_real_sources_from_source(source,
                                                                             False,
                                                                             return_groups=True)
        sources.append(source)
        for route in self.routes:
            for _source in sources:
                if _source.ip == source.ip:
                    _routes.append(copy(route))
        return unique(_routes)

    def get_sink_groups_from_member(self, sink: SinkDescription) -> List[SinkDescription]:
        """Returns all sink groups for the provided sink"""
        sink_groups: List[SinkDescription] = []
        for _sink in self.sinks:
            if sink.name in _sink.group_members:
                sink_groups.append(_sink)
                sink_groups.extend(self.get_sink_groups_from_member(_sink))
        return sink_groups

    def get_source_groups_from_member(self, source: SourceDescription) -> List[SourceDescription]:
        """Returns all source groups for the provided source"""
        source_groups: List[SourceDescription] = []
        for _source in self.sources:
            if source.name in _source.group_members:
                source_groups.append(_source)
                source_groups.extend(self.get_source_groups_from_member(_source))
        return source_groups

    def get_sink_from_name(self, name: SinkNameType) -> SinkDescription:
        """Returns a sink by name"""
        for sink in self.sinks:
            if sink.name == name:
                return sink
        return SinkDescription(name="Not Found")

    def get_source_from_name(self, name: SourceNameType) -> SourceDescription:
        """Get source by name"""
        for source in self.sources:
            if source.name == name:
                return source
        return SourceDescription(name="Not Found")

    def get_route_from_name(self, name: RouteNameType) -> RouteDescription:
        """Get route by name"""
        for route in self.routes:
            if route.name == name:
                return route
        return RouteDescription(name="Not Found", sink="", source="")
