/**
 * Extending the Window interface to include custom global functions and properties.
 */
interface Window {
  /**
   * Function to handle canvas click events.
   */
  canvasClick: () => void;

  /**
   * Function to handle keydown events on the canvas, accepting a KeyboardEvent as an argument.
   * @param event - The keyboard event triggered by the user.
   */
  canvasOnKeyDown: (event: KeyboardEvent) => void;

  /**
   * Function to start the visualizer with a specified IP address.
   * @param ip - The IP address of the visualizer server.
   */
  startVisualizer: (ip: string) => void;

  /**
   * Function to stop the visualizer.
   */
  stopVisualizer: () => void;
}
