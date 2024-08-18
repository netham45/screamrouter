"""Contains the logic to solve a configuration into sinks to sources with volume and equalization"""
from copy import copy
from typing import List, Optional

from src.screamrouter_types.annotations import (DelayType, RouteNameType,
                                                SinkNameType, SourceNameType, TimeshiftType,
                                                VolumeType)
from src.screamrouter_types.configuration import (Equalizer, RouteDescription,
                                                  SinkDescription,
                                                  SourceDescription)


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
                    # Make a copy and set the volume and equalizer and delay
                    real_source_copy: SourceDescription = copy(real_source)
                    real_source_copy.volume = real_source.volume * real_sink.volume
                    real_source_copy.equalizer = real_source.equalizer * real_sink.equalizer
                    real_source_copy.timeshift = real_source.timeshift + real_sink.timeshift
                    real_source_copy.delay = real_source.delay + real_sink.delay
                    # Check if there's already a sink entry in the dict, if so just append sources
                    sink_to_append_to: List[SinkDescription]
                    # Search for a sink by name
                    sink_to_append_to = [sink for sink in real_sinks_to_real_sources
                                              if sink.name == real_sink.name]
                    if len(sink_to_append_to) == 0: # If a sink was not found
                        if not real_sink in real_sinks_to_real_sources:
                            real_sinks_to_real_sources[real_sink] = []
                        real_sinks_to_real_sources[real_sink].append(real_source_copy)
                    else: # If a sink was found
                        sources_to_append_to: List[SourceDescription]
                        sources_to_append_to = real_sinks_to_real_sources[sink_to_append_to[0]]
                        # Check if the source is already on the sink, if so ignore it
                        found: bool = False
                        for source in sources_to_append_to:
                            if source.name == real_source_copy.name:
                                found = True
                                break
                        if not found:
                            sources_to_append_to.append(real_source_copy)
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
                                                                                  route.timeshift,
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
                                   timeshift_adjustment: Optional[TimeshiftType] = None,
                                   delay_adjustment: Optional[DelayType] = None,
                                   return_groups: bool = False
                                   ) -> List[SinkDescription]:
        """Resolves a sink group and returns a list of all sinks
        active_only controls if only enabled sinks are returned
        volume_adjustment (0.0-1.0) controls the multiplier for the volume of all the sinks
        equalizer_adjustment (0.0-1.0)[B1-B18] controls the multiplier for all the Equalizer bands
        delay_adjustment (0-5000) ms delay added to each sink
        return_groups controls if the group SinkDescriptions are left in or if they're removed"""
        # 1 is no change for volume and equalizer
        if volume_adjustment is None:
            volume_adjustment = 1
        if equalizer_adjustment is None:
            equalizer_adjustment = Equalizer()
        # 0 is no change for delay_adjustment
        if delay_adjustment is None:
            delay_adjustment = 0
        if timeshift_adjustment is None:
            timeshift_adjustment = 0

        # Figure out the adjustments
        adjusted_volume: VolumeType  = volume_adjustment * sink.volume
        adjusted_equalizer: Equalizer = equalizer_adjustment * sink.equalizer
        adjusted_delay: DelayType = delay_adjustment + sink.delay
        adjusted_timeshift: TimeshiftType = timeshift_adjustment + sink.timeshift

        # active_only means only return enabled groups/sinks
        if active_only and not sink.enabled:
            return []

        # If the sink is not a group then adjust it's volume, equalizer, delay
        # and return it in a list
        if not sink.is_group:
            sink_copy: SinkDescription = copy(sink)
            sink_copy.volume = adjusted_volume
            sink_copy.equalizer = adjusted_equalizer
            sink_copy.delay = adjusted_delay
            sink_copy.timeshift = adjusted_timeshift
            return [sink_copy]

        # Sink is a group, get all group members

        # Convert from List[SinkNameType] to List[SinkDescription]
        group_members: List[SinkDescription] = []
        for group_member_name in sink.group_members:
            group_member: SinkDescription = self.get_sink_from_name(group_member_name)
            group_members.append(group_member)

        # For each group member recursively return it's real sinks
        combined_group_members: List[SinkDescription] = []
        for group_member in group_members:
            combined_group_members.extend(self.get_real_sinks_from_sink(group_member,
                                                                              active_only,
                                                                              adjusted_volume,
                                                                              adjusted_equalizer,
                                                                              adjusted_timeshift,
                                                                              adjusted_delay,
                                                                              return_groups))
        # return_groups controls if the group SinkDescriptions are left in or if they're removed
        if return_groups:
            combined_group_members.append(sink)
        return combined_group_members

    def get_real_sources_from_source(self, source: SourceDescription,
                                   active_only: bool,
                                   volume_adjustment: Optional[VolumeType] = None,
                                   equalizer_adjustment: Optional[Equalizer] = None,
                                   timeshift_adjustment: Optional[TimeshiftType] = None,
                                   delay_adjustment: Optional[DelayType] = None,
                                   return_groups: bool = False
                                   ) -> List[SourceDescription]:
        """Resolves a source group and returns a list of all sources
        active_only controls if only enabled sources are returned
        volume_adjustment (0.0-1.0) controls the multiplier for the volume of all the sinks
        equalizer_adjustment (0.0-1.0)[B1-B18] controls the multiplier for all the Equalizer bands
        delay_adjustment (0-5000) ms delay added to each source
        return_groups controls if the group SourceDescriptions are left in or if they're removed"""
        # 1 is no change for volume and equalizer
        if volume_adjustment is None:
            volume_adjustment = 1
        if equalizer_adjustment is None:
            equalizer_adjustment = Equalizer()
        # 0 is no change for delay_adjustment
        if delay_adjustment is None:
            delay_adjustment = 0

        if timeshift_adjustment is None:
            timeshift_adjustment = 0

        # Figure out the adjustments
        adjusted_volume: VolumeType  = volume_adjustment * source.volume
        adjusted_equalizer: Equalizer = equalizer_adjustment * source.equalizer
        adjusted_delay: DelayType = delay_adjustment + source.delay
        adjusted_timeshift: TimeshiftType = timeshift_adjustment + source.timeshift

        # active_only means only return enabled groups/sources
        if active_only and not source.enabled:
            return []

        # If the source is not a group then adjust it's volume, equalizer, delay
        # and return it in a list
        if not source.is_group:
            source_copy: SourceDescription = copy(source)
            source_copy.volume = adjusted_volume
            source_copy.equalizer = adjusted_equalizer
            source_copy.delay = adjusted_delay
            source_copy.timeshift = adjusted_timeshift
            return [source_copy]

        # Source is a group, get all group members

        # Convert from List[SourceNameType] to List[SourceDescription]
        group_members: List[SourceDescription] = []
        for group_member_name in source.group_members:
            group_member: SourceDescription = self.get_source_from_name(group_member_name)
            group_members.append(group_member)

        # For each group member recursively return it's real sources
        combined_group_members: List[SourceDescription] = []
        for group_member in group_members:
            combined_group_members.extend(self.get_real_sources_from_source(group_member,
                                                                              active_only,
                                                                              adjusted_volume,
                                                                              adjusted_equalizer,
                                                                              adjusted_timeshift,
                                                                              adjusted_delay,
                                                                              return_groups))
        # return_groups controls if the group SourceDescriptions are left in or if they're removed
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
        # Get all sink names in a list
        sink_names: List[SinkNameType] = [sink.name for sink in sinks]
        routes_from_sinks: List[RouteDescription]
        # Go through each route and if it's name is in the sink list add it to the return
        routes_from_sinks = [copy(route) for route in self.routes if route.sink in sink_names]
        return unique(routes_from_sinks)

    def get_routes_by_source(self, source: SourceDescription) -> List[RouteDescription]:
        """Get all routes that use this source
           Volume levels for the returned routes will be adjusted based off the source levels
        """
        sources: List[SourceDescription] = self.get_real_sources_from_source(source,
                                                                             False,
                                                                             return_groups=True)
        # Get all sink names in a list
        source_names: List[SourceNameType] = [source.name for source in sources]
        routes_from_sources: List[RouteDescription]
        # Go through each route and if it's name is in the sink list add it to the return
        routes_from_sources = [copy(route) for route in self.routes if route.source in source_names]
        return unique(routes_from_sources)

    def get_sink_groups_from_member(self, sink: SinkDescription) -> List[SinkDescription]:
        """Returns all sink groups for the provided sink"""
        sink_groups: List[SinkDescription] = []
        for _sink in self.sinks:
            if sink.name in _sink.group_members:
                sink_groups.append(_sink)
                # Recursively add any parent groups
                sink_groups.extend(self.get_sink_groups_from_member(_sink))
        return sink_groups

    def get_source_groups_from_member(self, source: SourceDescription) -> List[SourceDescription]:
        """Returns all source groups for the provided source"""
        source_groups: List[SourceDescription] = []
        for _source in self.sources:
            if source.name in _source.group_members:
                source_groups.append(_source)
                # Recursively add any parent groups
                source_groups.extend(self.get_source_groups_from_member(_source))
        return source_groups

    def get_sink_from_name(self, name: SinkNameType) -> SinkDescription:
        """Returns a sink by name
           Returns a dummy sink named 'Not Found' if not found"""
        return ([sink for sink in self.sinks
                if sink.name == name] or [SinkDescription(name="Not Found")])[0]

    def get_source_from_name(self, name: SourceNameType) -> SourceDescription:
        """Get source by name
           Returns a dummy source named 'Not Found' if not found"""
        return ([source for source in self.sources
                if source.name == name] or [SourceDescription(name="Not Found")])[0]

    def get_route_from_name(self, name: RouteNameType) -> RouteDescription:
        """Get route by name
           Returns a dummy route named 'Not Found' if not found"""
        return ([route for route in self.routes
                if route.name == name] or [RouteDescription(name="Not Found",
                                                              sink="",
                                                              source="")])[0]
