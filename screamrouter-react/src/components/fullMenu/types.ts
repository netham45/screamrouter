import { Source, Sink, Route } from '../../api/api';
import { Actions } from '../../utils/actions';

/**
 * Type definition for view modes.
 */
export type ViewMode = 'grid' | 'list';

/**
 * Type definition for color modes.
 */
export type ColorMode = 'light' | 'dark' | 'system';

/**
 * Type definition for content categories.
 */
export type ContentCategory = 'dashboard' | 'active-source' | 'now-listening' | 'sources' | 'sinks' | 'routes' | 'favorites' | 'source' | 'sink' | 'route' | 'stats';

/**
 * Interface for the sort configuration.
 */
export interface SortConfig {
  key: string;
  direction: 'asc' | 'desc';
}

/**
 * Common props shared across content components
 */
export interface ContentProps {
  sources: Source[];
  sinks: Sink[];
  routes: Route[];
  starredSources: string[];
  starredSinks: string[];
  starredRoutes: string[];
  contextActiveSource: string | null;
  listeningToSink: Sink | null;
  viewMode: ViewMode;
  sortConfig: SortConfig;
  actions: Actions;
  isDarkMode?: boolean;
  sourceName?: string;
  sinkName?: string;
  routeName?: string;
  setCurrentCategory: (category: ContentCategory) => void;
  handleStar: (type: 'sources' | 'sinks' | 'routes', itemName: string) => void;
  handleToggleSource: (sourceName: string) => void;
  handleToggleSink: (sinkName: string) => void;
  handleToggleRoute: (routeName: string) => void;
  handleOpenSourceEqualizer: (sourceName: string) => void;
  handleOpenSinkEqualizer: (sinkName: string) => void;
  handleOpenRouteEqualizer: (routeName: string) => void;
  handleOpenVnc: (sourceName: string) => void;
  handleOpenVisualizer?: (sink: Sink) => void;
  handleUpdateSourceVolume: (sourceName: string, volume: number) => void;
  handleUpdateSinkVolume: (sinkName: string, volume: number) => void;
  handleUpdateSourceTimeshift: (sourceName: string, timeshift: number) => void;
  handleUpdateSinkTimeshift: (sinkName: string, timeshift: number) => void;
  handleUpdateRouteVolume: (routeName: string, volume: number) => void;
  handleUpdateRouteTimeshift: (routeName: string, timeshift: number) => void;
  handleControlSource?: (sourceName: string, action: 'prevtrack' | 'play' | 'nexttrack') => void;
}

/**
 * Props for the header bar component
 */
export interface HeaderBarProps {
  isDarkMode: boolean;
  sources: Source[];
  sinks: Sink[];
  routes: Route[];
  navigate: (type: 'sources' | 'sinks' | 'routes' | 'group-source' | 'group-sink', itemName: string) => void;
  toggleSidebar?: () => void;
  activeSource: string | null;
  controlSource?: (sourceName: string, action: 'prevtrack' | 'play' | 'nexttrack') => Promise<void>;
  updateVolume?: (type: 'sources' | 'sinks' | 'routes' | 'group-sink' | 'group-source', name: string, volume: number) => Promise<void>;
}

/**
 * Props for the sidebar component
 */
export interface SidebarProps {
  currentCategory: ContentCategory;
  setCurrentCategory: (category: ContentCategory) => void;
  sources: Source[];
  sinks: Sink[];
  routes: Route[];
  starredSources: string[];
  starredSinks: string[];
  starredRoutes: string[];
  sidebarOpen: boolean;
  toggleSidebar: () => void;
  openDesktopMenu: () => void;
  isDarkMode?: boolean;
}

/**
 * Props for the content header component
 */
export interface ContentHeaderProps {
  currentCategory: ContentCategory;
  viewMode: ViewMode;
  setViewMode: (mode: ViewMode) => void;
  sortConfig: SortConfig;
  onSort: (key: string) => void;
  isDarkMode?: boolean;
}

/**
 * Props for the empty state component
 */
export interface EmptyStateProps {
  icon: string;
  title: string;
  message: string;
  actionText?: string;
  onAction?: () => void;
}