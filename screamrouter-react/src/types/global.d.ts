interface Window {
  canvasClick: () => void;
  canvasOnKeyDown: (event: KeyboardEvent) => void;
  startVisualizer: (ip: string) => void;
  stopVisualizer: () => void;
}