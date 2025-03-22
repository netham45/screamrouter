/**
 * This file contains global functions and utilities used throughout the application.
 */

// Import the useAppContext hook from the AppContext module to access global application state.
import { useAppContext } from './context/AppContext';

// Import the ApiService for making API calls related to source control actions.
import ApiService from './api/api';

/**
 * Variable to hold the current application context, initialized as null.
 */
let appContext: ReturnType<typeof useAppContext> | null = null;

/**
 * Function to set the global application context.
 * @param context - The application context provided by the useAppContext hook.
 */
export const setAppContext = (context: ReturnType<typeof useAppContext>) => {
  appContext = context;
};

/**
 * Helper function to control the primary source based on the specified action.
 * @param action - The action to perform ('prevtrack', 'play', or 'nexttrack').
 */
const controlPrimarySource = async (action: 'prevtrack' | 'play' | 'nexttrack') => {
  if (!appContext) {
    console.error('App context is not set');
    return;
  }

  // Find the primary source from the list of sources in the application context.
  const primarySource = appContext.sources.find(source => source.is_primary);
  if (!primarySource) {
    console.error('No primary source found');
    return;
  }

  try {
    // Perform the specified action on the primary source via the ApiService.
    await ApiService.controlSource(primarySource.name, action);
    console.log(`${action} action performed on primary source: ${primarySource.name}`);
  } catch (error) {
    console.error(`Error controlling primary source (${action}):`, error);
  }
};

/**
 * Function to play the previous song on the primary source.
 */
export const previousSongOnPrimarySource = async () => {
  await controlPrimarySource('prevtrack');
};

/**
 * Function to toggle play/pause on the primary source.
 */
export const playPauseOnPrimarySource = async () => {
  await controlPrimarySource('play');
};

/**
 * Function to play the next song on the primary source.
 */
export const nextSongOnPrimarySource = async () => {
  await controlPrimarySource('nexttrack');
};

/**
 * Function called when the Desktop Menu is shown.
 * The actual color mode changing functionality will be provided by GlobalFunctionsComponent.
 */
export const DesktopMenuShow = () => {
  // The actual implementation will be overridden by GlobalFunctionsComponent
  console.log("DesktopMenuShow called - this is the original implementation");
  
  // Just dispatch resize event, the actual color mode change will be handled by the component
  window.dispatchEvent(new Event('resize'));
};

/**
 * Function called when the Desktop Menu is hidden.
 * Currently doesn't perform any actions, but is included for future extensibility.
 */
export const DesktopMenuHide = () => {
  // No implementation required at this time
  // This function is included for future use
};

// Extend the global window object to include functions for controlling the primary source.
declare global {
  interface Window {
    previousSongOnPrimarySource: () => Promise<void>;
    playPauseOnPrimarySource: () => Promise<void>;
    nextSongOnPrimarySource: () => Promise<void>;
    DesktopMenuShow: () => void;
    DesktopMenuHide: () => void;
  }
}

// Assign the control functions to the global window object for easy access.
window.previousSongOnPrimarySource = previousSongOnPrimarySource;
window.playPauseOnPrimarySource = playPauseOnPrimarySource;
window.nextSongOnPrimarySource = nextSongOnPrimarySource;
window.DesktopMenuShow = DesktopMenuShow;
window.DesktopMenuHide = DesktopMenuHide;
