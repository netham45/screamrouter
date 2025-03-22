import { Source, Sink, Route } from '../../api/api';
import { SortConfig, ContentCategory } from './types';

/**
 * Function to open a URL in a new window.
 */
export const openInNewWindow = (url: string, width: number = 800, height: number = 600) => {
  const left = (window.screen.width - width) / 2;
  const top = (window.screen.height - height) / 2;
  window.open(url, '_blank', `width=${width},height=${height},left=${left},top=${top}`);
};

/**
 * Function to open the edit page for a source, sink, route, or group.
 */
export const openEditPage = (type: 'sources' | 'sinks' | 'routes' | 'group-sink' | 'group-source', item: { name: string }) => {
  let url;
  
  switch (type) {
    case 'sources':
      url = `/site/edit-source?name=${encodeURIComponent(item.name)}`;
      break;
    case 'sinks':
      url = `/site/edit-sink?name=${encodeURIComponent(item.name)}`;
      break;
    case 'routes':
      url = `/site/edit-route?name=${encodeURIComponent(item.name)}`;
      break;
    case 'group-sink':
      url = `/site/edit-group?type=sink&name=${encodeURIComponent(item.name)}`;
      break;
    case 'group-source':
      url = `/site/edit-group?type=source&name=${encodeURIComponent(item.name)}`;
      break;
    default:
      return;
  }
  
  openInNewWindow(url, 800, 700);
};

/**
 * Function to open the add page for a source, sink, route, or group.
 */
export const openAddPage = (type: 'sources' | 'sinks' | 'routes' | 'group-sink' | 'group-source') => {
  let url;
  
  switch (type) {
    case 'sources':
      url = '/site/add-source';
      break;
    case 'sinks':
      url = '/site/add-sink';
      break;
    case 'routes':
      url = '/site/add-route';
      break;
    case 'group-sink':
      url = '/site/add-group?type=sink';
      break;
    case 'group-source':
      url = '/site/add-group?type=source';
      break;
    default:
      return;
  }
  
  openInNewWindow(url, 800, 700);
};

/**
 * Function to open the add route page with a pre-selected source or sink.
 */
export const openAddRouteWithPreselection = (type: 'source' | 'sink', name: string) => {
  const url = `/site/add-route?${type}=${encodeURIComponent(name)}`;
  openInNewWindow(url, 800, 700);
};

/**
 * Function to apply the color mode to the document body.
 */
export const applyColorMode = (mode: 'light' | 'dark' | 'system') => {
  if (mode === 'system') {
    const prefersDark = window.matchMedia('(prefers-color-scheme: dark)').matches;
    document.body.classList.toggle('dark-mode', prefersDark);
  } else {
    document.body.classList.toggle('dark-mode', mode === 'dark');
  }
};

/**
 * Function to scroll to an element by ID.
 * @param id The ID of the element to scroll to
 * @param delay Optional delay in milliseconds before scrolling
 */
export const scrollToElement = (id: string, delay: number = 100) => {
  setTimeout(() => {
    const element = document.getElementById(id);
    if (element) {
      element.scrollIntoView({ behavior: 'smooth', block: 'center' });
      // Add a temporary highlight effect
      element.classList.add('highlight-item');
      setTimeout(() => {
        element.classList.remove('highlight-item');
      }, 2000);
    }
  }, delay);
};

/**
 * Function to get the title for the current category.
 */
export const getCategoryTitle = (currentCategory: ContentCategory): string => {
  switch (currentCategory) {
    case 'dashboard':
      return 'Dashboard';
    case 'active-source':
      return 'Primary Source';
    case 'now-listening':
      return 'Now Listening';
    case 'sources':
      return 'Sources';
    case 'sinks':
      return 'Sinks';
    case 'routes':
      return 'Routes';
    case 'favorites':
      return 'Favorites';
    default:
      return 'ScreamRouter';
  }
};

/**
 * Function to sort sources based on the current sort configuration.
 */
export const sortSources = (sources: Source[], sortConfig: SortConfig, starredSources: string[], contextActiveSource: string | null): Source[] => {
  return [...sources].sort((a, b) => {
    if (sortConfig.key === 'favorite') {
      const aStarred = starredSources.includes(a.name);
      const bStarred = starredSources.includes(b.name);
      return sortConfig.direction === 'asc' ? (aStarred === bStarred ? 0 : aStarred ? -1 : 1) : (aStarred === bStarred ? 0 : aStarred ? 1 : -1);
    }
    if (sortConfig.key === 'active') {
      const aActive = contextActiveSource === a.name;
      const bActive = contextActiveSource === b.name;
      return sortConfig.direction === 'asc' ? (aActive === bActive ? 0 : aActive ? -1 : 1) : (aActive === bActive ? 0 : aActive ? 1 : -1);
    }
    const aValue = a[sortConfig.key as keyof Source];
    const bValue = b[sortConfig.key as keyof Source];
    if (aValue === undefined || bValue === undefined) {
      return 0;
    }
    if (typeof aValue === 'string' && typeof bValue === 'string') {
      return sortConfig.direction === 'asc' ? aValue.localeCompare(bValue) : bValue.localeCompare(aValue);
    }
    if (aValue < bValue) return sortConfig.direction === 'asc' ? -1 : 1;
    if (aValue > bValue) return sortConfig.direction === 'asc' ? 1 : -1;
    return 0;
  });
};

/**
 * Function to sort sinks based on the current sort configuration.
 */
export const sortSinks = (sinks: Sink[], sortConfig: SortConfig, starredSinks: string[], listeningToSink: Sink | null): Sink[] => {
  return [...sinks].sort((a, b) => {
    if (sortConfig.key === 'favorite') {
      const aStarred = starredSinks.includes(a.name);
      const bStarred = starredSinks.includes(b.name);
      return sortConfig.direction === 'asc' ? (aStarred === bStarred ? 0 : aStarred ? -1 : 1) : (aStarred === bStarred ? 0 : aStarred ? 1 : -1);
    }
    if (sortConfig.key === 'active') {
      const aActive = listeningToSink?.name === a.name;
      const bActive = listeningToSink?.name === b.name;
      return sortConfig.direction === 'asc' ? (aActive === bActive ? 0 : aActive ? -1 : 1) : (aActive === bActive ? 0 : aActive ? 1 : -1);
    }
    const aValue = a[sortConfig.key as keyof Sink];
    const bValue = b[sortConfig.key as keyof Sink];
    if (aValue === undefined || bValue === undefined) {
      return 0;
    }
    if (typeof aValue === 'string' && typeof bValue === 'string') {
      return sortConfig.direction === 'asc' ? aValue.localeCompare(bValue) : bValue.localeCompare(aValue);
    }
    if (aValue < bValue) return sortConfig.direction === 'asc' ? -1 : 1;
    if (aValue > bValue) return sortConfig.direction === 'asc' ? 1 : -1;
    return 0;
  });
};

/**
 * Function to sort routes based on the current sort configuration.
 */
export const sortRoutes = (routes: Route[], sortConfig: SortConfig, starredRoutes: string[]): Route[] => {
  return [...routes].sort((a, b) => {
    if (sortConfig.key === 'favorite') {
      const aStarred = starredRoutes.includes(a.name);
      const bStarred = starredRoutes.includes(b.name);
      return sortConfig.direction === 'asc' ? (aStarred === bStarred ? 0 : aStarred ? -1 : 1) : (aStarred === bStarred ? 0 : aStarred ? 1 : -1);
    }
    const aValue = a[sortConfig.key as keyof Route];
    const bValue = b[sortConfig.key as keyof Route];
    if (aValue === undefined || bValue === undefined) {
      return 0;
    }
    if (typeof aValue === 'string' && typeof bValue === 'string') {
      return sortConfig.direction === 'asc' ? aValue.localeCompare(bValue) : bValue.localeCompare(aValue);
    }
    if (aValue < bValue) return sortConfig.direction === 'asc' ? -1 : 1;
    if (aValue > bValue) return sortConfig.direction === 'asc' ? 1 : -1;
    return 0;
  });
};