"""Contains the logic to solve a configuration into sinks to sources with volume and equalization"""
from copy import copy, deepcopy
from typing import Dict, List, Optional, Set

from screamrouter.screamrouter_types.annotations import (DelayType, RouteNameType,
                                                SinkNameType, SourceNameType,
                                                TimeshiftType, VolumeType)
from screamrouter.screamrouter_types.configuration import (Equalizer, RouteDescription,
                                                  SinkDescription,
                                                  SourceDescription,
                                                  SpeakerLayout)


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

    def _generate_cpp_default_matrix(self, input_channels: int, output_channels: int) -> List[List[float]]:
        """Generates the default C++ speaker mixing matrix."""
        matrix = [[(0.0) for _ in range(8)] for _ in range(8)] # Start with a zero matrix

        # Mimic the C++ logic from speaker_mix.cpp / SpeakerLayoutPage.tsx
        # FL=0, FR=1, C=2, LFE=3, BL=4, BR=5, SL=6, SR=7
        # Ensure output_channels is at least 1 to avoid issues with min(outputChannels, 8)
        safe_output_channels = max(1, output_channels)

        if input_channels == 1: # Mono input
            for i in range(min(safe_output_channels, 8)):
                matrix[0][i] = 1.0
        elif input_channels == 2: # Stereo input
            if safe_output_channels == 1:
                matrix[0][0] = 0.5; matrix[1][0] = 0.5
            elif safe_output_channels == 2:
                matrix[0][0] = 1.0; matrix[1][1] = 1.0
            elif safe_output_channels == 4: # Stereo -> Quad
                matrix[0][0] = 1.0; matrix[0][2] = 1.0 # FL -> FL, BL
                matrix[1][1] = 1.0; matrix[1][3] = 1.0 # FR -> FR, BR
            elif safe_output_channels == 6: # Stereo -> 5.1
                matrix[0][0] = 1.0  # FL to FL
                matrix[1][1] = 1.0  # FR to FR
                matrix[0][2] = 0.5; matrix[1][2] = 0.5  # L/R to Center
                # LFE is often silent or derived, C++ code has it commented out for stereo->5.1
                matrix[0][4] = 1.0  # FL to BL (or SL in 5.1)
                matrix[1][5] = 1.0  # FR to BR (or SR in 5.1)
            elif safe_output_channels >= 8: # Stereo -> 7.1 (or more, map to 7.1 pattern)
                matrix[0][0] = 1.0  # FL to FL
                matrix[1][1] = 1.0  # FR to FR
                matrix[0][2] = 0.5; matrix[1][2] = 0.5  # L/R to Center
                matrix[0][4] = 1.0  # FL to BL
                matrix[1][5] = 1.0  # FR to BR
                matrix[0][6] = 1.0  # FL to SL
                matrix[1][7] = 1.0  # FR to SR
        elif input_channels == 4: # Quad input (assuming FL, FR, RL, RR)
            # C++ code implies FL,FR,C,LFE for 4ch input in some cases, then FL,FR,RL,RR.
            # Using FL,FR,RL,RR as more standard for "Quad" source.
            # Indices: FL=0, FR=1, RL=2, RR=3 for input
            if safe_output_channels == 1: # Quad -> Mono
                matrix[0][0] = 0.25; matrix[1][0] = 0.25; matrix[2][0] = 0.25; matrix[3][0] = 0.25
            elif safe_output_channels == 2: # Quad -> Stereo
                matrix[0][0] = 0.5; matrix[2][0] = 0.5 # FL, RL to L
                matrix[1][1] = 0.5; matrix[3][1] = 0.5 # FR, RR to R
            elif safe_output_channels == 4: # Quad -> Quad
                matrix[0][0]=1.0; matrix[1][1]=1.0; matrix[2][2]=1.0; matrix[3][3]=1.0
            elif safe_output_channels == 6: # Quad -> 5.1
                matrix[0][0]=1.0; matrix[1][1]=1.0 # FL, FR
                matrix[0][2]=0.5; matrix[1][2]=0.5 # Center from FL, FR
                # LFE from all
                matrix[0][3]=0.25; matrix[1][3]=0.25; matrix[2][3]=0.25; matrix[3][3]=0.25
                matrix[2][4]=1.0; matrix[3][5]=1.0 # RL, RR
            elif safe_output_channels >= 8: # Quad -> 7.1
                matrix[0][0]=1.0; matrix[1][1]=1.0 # FL, FR
                matrix[0][2]=0.5; matrix[1][2]=0.5 # Center
                matrix[0][3]=0.25; matrix[1][3]=0.25; matrix[2][3]=0.25; matrix[3][3]=0.25 # LFE
                matrix[2][4]=1.0; matrix[3][5]=1.0 # RL, RR
                # Side from corresponding front/rear
                matrix[0][6]=0.5; matrix[2][6]=0.5 # SL from FL, RL
                matrix[1][7]=0.5; matrix[3][7]=0.5 # SR from FR, RR
        elif input_channels == 6: # 5.1 input (FL,FR,C,LFE,RL,RR)
            if safe_output_channels == 1: # 5.1 -> Mono
                matrix[0][0]=0.2; matrix[1][0]=0.2; matrix[2][0]=0.2; matrix[4][0]=0.2; matrix[5][0]=0.2 # LFE (idx 3) usually not in mono mix
            elif safe_output_channels == 2: # 5.1 -> Stereo
                matrix[0][0]=0.33; matrix[4][0]=0.33 # FL, RL to L
                matrix[1][1]=0.33; matrix[5][1]=0.33 # FR, RR to R
                matrix[2][0]=0.33; matrix[2][1]=0.33 # Center to L/R
            elif safe_output_channels == 4: # 5.1 -> Quad
                matrix[0][0]=0.66; matrix[2][0]=0.33 # FL, C to FL out
                matrix[1][1]=0.66; matrix[2][1]=0.33 # FR, C to FR out
                matrix[4][2]=1.0; matrix[5][3]=1.0   # RL, RR to RL, RR out
            elif safe_output_channels == 6: # 5.1 -> 5.1
                for i in range(6): matrix[i][i]=1.0
            elif safe_output_channels >= 8: # 5.1 -> 7.1
                for i in range(6): matrix[i][i]=1.0 # Direct map for first 6
                matrix[0][6]=0.5; matrix[4][6]=0.5 # SL from FL, RL
                matrix[1][7]=0.5; matrix[5][7]=0.5 # SR from FR, RR
        elif input_channels == 8: # 7.1 input (FL,FR,C,LFE,RL,RR,SL,SR)
            if safe_output_channels == 1: # 7.1 -> Mono
                val = 1.0/7.0
                matrix[0][0]=val; matrix[1][0]=val; matrix[2][0]=val; matrix[4][0]=val; matrix[5][0]=val; matrix[6][0]=val; matrix[7][0]=val # LFE (idx 3) out
            elif safe_output_channels == 2: # 7.1 -> Stereo
                matrix[0][0]=0.5; matrix[4][0]=0.125; matrix[6][0]=0.125 # FL,RL,SL to L
                matrix[1][1]=0.5; matrix[5][1]=0.125; matrix[7][1]=0.125 # FR,RR,SR to R
                matrix[2][0]=0.25; matrix[2][1]=0.25 # Center to L/R
            elif safe_output_channels == 4: # 7.1 -> Quad
                matrix[0][0]=0.5; matrix[6][0]=0.25; matrix[2][0]=0.25 # FL,SL,C to FL out
                matrix[1][1]=0.5; matrix[7][1]=0.25; matrix[2][1]=0.25 # FR,SR,C to FR out
                matrix[4][2]=0.66; matrix[6][2]=0.33 # RL,SL to RL out
                matrix[5][3]=0.66; matrix[7][3]=0.33 # RR,SR to RR out
            elif safe_output_channels == 6: # 7.1 -> 5.1
                matrix[0][0]=0.66; matrix[6][0]=0.33 # FL,SL to FL
                matrix[1][1]=0.66; matrix[7][1]=0.33 # FR,SR to FR
                matrix[2][2]=1.0; matrix[3][3]=1.0 # C, LFE
                matrix[4][4]=0.66; matrix[6][4]=0.33 # RL,SL to RL
                matrix[5][5]=0.66; matrix[7][5]=0.33 # RR,SR to RR
            elif safe_output_channels >= 8: # 7.1 -> 7.1
                for i in range(8): matrix[i][i]=1.0
        else: # Fallback for unsupported input channel counts (or if input_channels is 0)
            min_ch_default = min(input_channels if input_channels > 0 else 8, safe_output_channels, 8) # Ensure input_channels > 0 for min
            for i in range(min_ch_default):
                matrix[i][i] = 1.0
        return matrix

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
        for route_description, real_sinks in routes_to_real_sinks.items():
            for real_sink in real_sinks:
                for real_source in routes_to_real_sources[route_description]:
                    real_source_copy: SourceDescription = copy(real_source) # real_source already has its layouts processed by route
                    
                    real_source_copy.volume = real_source.volume * real_sink.volume * route_description.volume
                    real_source_copy.equalizer = real_source.equalizer * real_sink.equalizer * route_description.equalizer
                    real_source_copy.timeshift = real_source.timeshift + real_sink.timeshift + route_description.timeshift
                    real_source_copy.delay = real_source.delay + real_sink.delay + route_description.delay

                    # --- SpeakerLayouts Handling ---
                    # real_source.speaker_layouts has been processed by route's speaker_layouts_adjustment.
                    # real_sink.speaker_layouts has been processed by route's speaker_layouts_adjustment (if sink was part of a group in route).
                    # Now, combine these, and then combine with the route's original speaker_layouts.

                    real_source_copy.delay = real_source.delay + real_sink.delay + route_description.delay

                    # --- SpeakerLayouts Handling (Inlined) ---
                    # real_source.speaker_layouts contains the (Source * SourceGroups) product, already resolved.
                    # real_sink.speaker_layouts contains the (Sink * SinkGroups) product, already resolved.
                    # route_description.speaker_layouts contains the Route's own layouts.

                    identity_matrix = [[(1.0 if i == j else 0.0) for j in range(8)] for i in range(8)]
                    identity_sl = SpeakerLayout(matrix=identity_matrix, auto_mode=False)

                    # 1. Resolve Route's layouts (auto_mode -> identity for routes)
                    resolved_route_layouts: Dict[int, SpeakerLayout] = {}
                    # Ensure route_description.speaker_layouts is a dict, even if empty or None
                    route_own_layouts = route_description.speaker_layouts if route_description.speaker_layouts is not None else {}
                    for ch_key, r_layout_obj_original in route_own_layouts.items():
                        r_layout_obj = r_layout_obj_original if r_layout_obj_original is not None else SpeakerLayout()
                        matrix_to_use: List[List[float]]
                        if r_layout_obj.auto_mode:
                            matrix_to_use = identity_matrix
                        else:
                            matrix_to_use = r_layout_obj.matrix if r_layout_obj.matrix is not None else identity_matrix
                        resolved_route_layouts[ch_key] = SpeakerLayout(matrix=matrix_to_use, auto_mode=False)

                    # 2. Multiply (Source*Groups) by (Resolved Route Layouts)
                    source_x_route_layouts: Dict[int, SpeakerLayout] = {}
                    source_group_layouts = real_source.speaker_layouts if real_source.speaker_layouts is not None else {}
                    
                    all_keys_sr = set(list(source_group_layouts.keys()) + list(resolved_route_layouts.keys()))
                    for key_sr in all_keys_sr:
                        s_layout = source_group_layouts.get(key_sr, identity_sl)
                        r_layout = resolved_route_layouts.get(key_sr, identity_sl)
                        s_layout = s_layout if s_layout is not None else identity_sl # Ensure not None
                        r_layout = r_layout if r_layout is not None else identity_sl # Ensure not None
                        source_x_route_layouts[key_sr] = s_layout * r_layout
                    
                    # 3. Multiply ((Source*Groups)*Route) by (Sink*Groups)
                    final_speaker_layouts: Dict[int, SpeakerLayout] = {}
                    sink_group_layouts = real_sink.speaker_layouts if real_sink.speaker_layouts is not None else {}

                    all_keys_final = set(list(source_x_route_layouts.keys()) + list(sink_group_layouts.keys()))
                    for key_final in all_keys_final:
                        sr_layout = source_x_route_layouts.get(key_final, identity_sl)
                        sk_layout = sink_group_layouts.get(key_final, identity_sl)
                        sr_layout = sr_layout if sr_layout is not None else identity_sl # Ensure not None
                        sk_layout = sk_layout if sk_layout is not None else identity_sl # Ensure not None
                        final_speaker_layouts[key_final] = sr_layout * sk_layout
                    
                    real_source_copy.speaker_layouts = final_speaker_layouts
                    # --- End SpeakerLayouts Handling ---

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
                                                                                   route.delay,
                                                                                   # Pass empty dict, route layouts applied later
                                                                                   {})
        return routes_to_real_sources

    def __get_routes_to_real_sinks(self) -> dict[RouteDescription, List[SinkDescription]]:
        """Returns a map of routes to real enabled sinks
           that has been adjusted for volume and equalization"""
        routes_to_real_sinks: dict[RouteDescription, List[SinkDescription]] = {}
        for route in self.routes:
            if route.enabled:
                # Pass the route's speaker_layouts as the adjustment for sinks resolved via this route
                sink: SinkDescription = self.get_sink_from_name(route.sink)
                routes_to_real_sinks[route] = self.get_real_sinks_from_sink(
                    sink, 
                    True,
                    volume_adjustment=route.volume,
                    equalizer_adjustment=route.equalizer,
                    timeshift_adjustment=route.timeshift,
                    delay_adjustment=route.delay,
                    # Pass empty dict, route layouts applied later
                    speaker_layouts_adjustment={}
                )
        return routes_to_real_sinks

    def get_real_sinks_from_sink(self, sink: SinkDescription,
                                   active_only: bool,
                                   volume_adjustment: Optional[VolumeType] = None,
                                   equalizer_adjustment: Optional[Equalizer] = None,
                                    timeshift_adjustment: Optional[TimeshiftType] = None,
                                    delay_adjustment: Optional[DelayType] = None,
                                    # speaker_layouts_adjustment is now used to combine with sink's own layouts
                                    speaker_layouts_adjustment: Optional[Dict[int, SpeakerLayout]] = None, 
                                    return_groups: bool = False
                                    ) -> List[SinkDescription]:
        """Resolves a sink group and returns a list of all sinks.
        Speaker layouts from 'speaker_layouts_adjustment' (e.g., from a route)
        are now multiplied with the sink's own layouts.
        """
        if volume_adjustment is None: volume_adjustment = 1.0
        if equalizer_adjustment is None: equalizer_adjustment = Equalizer()
        if delay_adjustment is None: delay_adjustment = 0
        if timeshift_adjustment is None: timeshift_adjustment = 0
        if speaker_layouts_adjustment is None: speaker_layouts_adjustment = {}
        parent_group_layouts = speaker_layouts_adjustment # Renaming for clarity

        adjusted_volume: VolumeType  = volume_adjustment * sink.volume
        adjusted_equalizer: Equalizer = equalizer_adjustment * sink.equalizer
        adjusted_delay: DelayType = delay_adjustment + sink.delay
        adjusted_timeshift: TimeshiftType = timeshift_adjustment + sink.timeshift
        
        identity_matrix = [[(1.0 if i == j else 0.0) for j in range(8)] for i in range(8)]
        identity_sl = SpeakerLayout(matrix=identity_matrix, auto_mode=False)

        # Resolve current sink's own layouts and multiply with parent_group_layouts
        current_sink_own_layouts_orig = sink.speaker_layouts if sink.speaker_layouts is not None else {}
        # Deepcopy to prevent modification of original configuration objects
        current_sink_own_layouts = deepcopy(current_sink_own_layouts_orig)

        # This variable will store the product of (parent_group_layouts * current_sink_resolved_layout)
        processed_layouts_for_sink: Dict[int, SpeakerLayout] = {}
        
        all_ch_keys = set(list(parent_group_layouts.keys()) + list(current_sink_own_layouts.keys()))

        for key_input_ch in all_ch_keys:
            parent_layout_sl = parent_group_layouts.get(key_input_ch, identity_sl)
            parent_layout_sl = parent_layout_sl if parent_layout_sl is not None else identity_sl

            sl_obj_original = current_sink_own_layouts.get(key_input_ch)
            sl_obj = sl_obj_original if sl_obj_original is not None else SpeakerLayout() # Default to auto

            matrix_to_use: List[List[float]]
            if sl_obj.auto_mode:
                if not sink.is_group: # It's a physical sink
                    matrix_to_use = self._generate_cpp_default_matrix(key_input_ch, sink.channels if sink.channels is not None else 0)
                else: # It's a sink group, auto means identity for the group itself
                    matrix_to_use = identity_matrix
            else: # Manual mode
                matrix_to_use = sl_obj.matrix if sl_obj.matrix is not None else identity_matrix
            
            resolved_sl_for_current_sink_component = SpeakerLayout(matrix=matrix_to_use, auto_mode=False)
            
            processed_layouts_for_sink[key_input_ch] = parent_layout_sl * resolved_sl_for_current_sink_component

        if active_only and not sink.enabled:
            return []

        if not sink.is_group:
            sink_copy: SinkDescription = copy(sink)
            sink_copy.volume = adjusted_volume
            sink_copy.equalizer = adjusted_equalizer
            sink_copy.delay = adjusted_delay
            sink_copy.timeshift = adjusted_timeshift
            sink_copy.speaker_layouts = processed_layouts_for_sink # This is Sink * ParentGroups
            return [sink_copy]

        # Sink is a group, get all group members
        group_members_desc: List[SinkDescription] = []
        for member_name in sink.group_members:
            group_members_desc.append(self.get_sink_from_name(member_name))

        combined_group_members: List[SinkDescription] = []
        for member_sink in group_members_desc:
            # When recursing, 'processed_layouts_for_sink' (which is current_sink_group_layout * parent_group_layouts)
            # becomes the new 'speaker_layouts_adjustment' for the member.
            combined_group_members.extend(self.get_real_sinks_from_sink(
                member_sink,
                active_only,
                adjusted_volume, 
                adjusted_equalizer, 
                adjusted_timeshift, 
                adjusted_delay, 
                processed_layouts_for_sink, # Pass down the accumulated product
                return_groups
            ))
        if return_groups:
            group_sink_copy = copy(sink) # The group itself
            group_sink_copy.volume = adjusted_volume
            group_sink_copy.equalizer = adjusted_equalizer
            group_sink_copy.delay = adjusted_delay
            group_sink_copy.timeshift = adjusted_timeshift
            group_sink_copy.speaker_layouts = processed_layouts_for_sink # Group also gets processed layouts
            combined_group_members.append(group_sink_copy)
        return combined_group_members

    def get_real_sources_from_source(self, source: SourceDescription,
                                   active_only: bool,
                                   volume_adjustment: Optional[VolumeType] = None,
                                   equalizer_adjustment: Optional[Equalizer] = None,
                                   timeshift_adjustment: Optional[TimeshiftType] = None,
                                   delay_adjustment: Optional[DelayType] = None,
                                   speaker_layouts_adjustment: Optional[Dict[int, SpeakerLayout]] = None,
                                   return_groups: bool = False
                                   ) -> List[SourceDescription]:
        """
        Resolves a source group and returns a list of all real sources.
        The source's own speaker_layouts are multiplied by the speaker_layouts_adjustment
        (accumulated product from parent groups).
        Auto_mode layouts for sources or groups are resolved to identity.
        """
        if volume_adjustment is None: volume_adjustment = 1.0
        if equalizer_adjustment is None: equalizer_adjustment = Equalizer()
        if delay_adjustment is None: delay_adjustment = 0
        if timeshift_adjustment is None: timeshift_adjustment = 0
        parent_group_layouts = speaker_layouts_adjustment if speaker_layouts_adjustment is not None else {}

        adjusted_volume: VolumeType = volume_adjustment * source.volume
        adjusted_equalizer: Equalizer = equalizer_adjustment * source.equalizer
        adjusted_delay: DelayType = delay_adjustment + source.delay
        adjusted_timeshift: TimeshiftType = timeshift_adjustment + source.timeshift

        identity_matrix = [[(1.0 if i == j else 0.0) for j in range(8)] for i in range(8)]
        identity_sl = SpeakerLayout(matrix=identity_matrix, auto_mode=False)

        # Resolve current source's own layouts and multiply with parent_group_layouts
        current_source_own_layouts_orig = source.speaker_layouts if source.speaker_layouts is not None else {}
        current_source_own_layouts = deepcopy(current_source_own_layouts_orig) # Deepcopy for safety
        
        processed_layouts_for_source: Dict[int, SpeakerLayout] = {}
        
        all_ch_keys = set(list(parent_group_layouts.keys()) + list(current_source_own_layouts.keys()))

        for key_input_ch in all_ch_keys:
            parent_layout_sl = parent_group_layouts.get(key_input_ch, identity_sl)
            parent_layout_sl = parent_layout_sl if parent_layout_sl is not None else identity_sl

            sl_obj_original = current_source_own_layouts.get(key_input_ch)
            sl_obj = sl_obj_original if sl_obj_original is not None else SpeakerLayout()

            matrix_to_use: List[List[float]]
            if sl_obj.auto_mode: # Sources and source groups resolve auto to identity
                matrix_to_use = identity_matrix
            else:
                matrix_to_use = sl_obj.matrix if sl_obj.matrix is not None else identity_matrix
            
            resolved_sl_for_current_source_component = SpeakerLayout(matrix=matrix_to_use, auto_mode=False)
            processed_layouts_for_source[key_input_ch] = parent_layout_sl * resolved_sl_for_current_source_component

        if active_only and not source.enabled:
            return []

        if not source.is_group:
            source_copy: SourceDescription = copy(source)
            source_copy.volume = adjusted_volume
            source_copy.equalizer = adjusted_equalizer
            source_copy.delay = adjusted_delay
            source_copy.timeshift = adjusted_timeshift
            source_copy.speaker_layouts = processed_layouts_for_source # This is Source * ParentGroups
            return [source_copy]

        group_members_desc: List[SourceDescription] = []
        for member_name in source.group_members:
            group_members_desc.append(self.get_source_from_name(member_name))

        combined_group_members: List[SourceDescription] = []
        for member_source in group_members_desc:
            combined_group_members.extend(self.get_real_sources_from_source(
                member_source,
                active_only,
                adjusted_volume,
                adjusted_equalizer,
                adjusted_timeshift,
                adjusted_delay,
                processed_layouts_for_source, # Pass down accumulated product
                return_groups
            ))
        if return_groups:
            group_source_copy = copy(source) # The group itself
            group_source_copy.volume = adjusted_volume
            group_source_copy.equalizer = adjusted_equalizer
            group_source_copy.delay = adjusted_delay
            group_source_copy.timeshift = adjusted_timeshift
            group_source_copy.speaker_layouts = processed_layouts_for_source
            combined_group_members.append(group_source_copy)
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
