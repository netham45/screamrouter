import { useAppContext } from './context/AppContext';
import ApiService from './api/api';

let appContext: ReturnType<typeof useAppContext> | null = null;

export const setAppContext = (context: ReturnType<typeof useAppContext>) => {
  appContext = context;
};

const controlPrimarySource = async (action: 'prevtrack' | 'play' | 'nexttrack') => {
  if (!appContext) {
    console.error('App context is not set');
    return;
  }

  const primarySource = appContext.sources.find(source => source.is_primary);
  if (!primarySource) {
    console.error('No primary source found');
    return;
  }

  try {
    await ApiService.controlSource(primarySource.name, action);
    console.log(`${action} action performed on primary source: ${primarySource.name}`);
  } catch (error) {
    console.error(`Error controlling primary source (${action}):`, error);
  }
};

export const previousSongOnPrimarySource = async () => {
  await controlPrimarySource('prevtrack');
};

export const playPauseOnPrimarySource = async () => {
  await controlPrimarySource('play');
};

export const nextSongOnPrimarySource = async () => {
  await controlPrimarySource('nexttrack');
};

// Add these functions to the global window object
declare global {
  interface Window {
    previousSongOnPrimarySource: () => Promise<void>;
    playPauseOnPrimarySource: () => Promise<void>;
    nextSongOnPrimarySource: () => Promise<void>;
  }
}

window.previousSongOnPrimarySource = previousSongOnPrimarySource;
window.playPauseOnPrimarySource = playPauseOnPrimarySource;
window.nextSongOnPrimarySource = nextSongOnPrimarySource;
