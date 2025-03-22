declare module 'butterchurn' {
  // Define a type for the preset object
  interface Preset {
    baseVals: Record<string, number>;
    waves: Array<Record<string, number | string>>;
    init_eqs: Record<string, number>;
    [key: string]: unknown;
  }

  interface VisualizerOptions {
    width: number;
    height: number;
    pixelRatio: number;
    textureRatio: number;
  }

  interface Visualizer {
    setRendererSize: (width: number, height: number) => void;
    connectAudio: (audioNode: AudioNode) => void;
    render: () => void;
    loadPreset: (preset: Preset, blendTime: number) => void;
  }

  function createVisualizer(
    audioContext: AudioContext,
    canvas: HTMLCanvasElement,
    options: VisualizerOptions
  ): Visualizer;

  export { createVisualizer, Visualizer, VisualizerOptions, Preset };
  export default { createVisualizer };
}

declare module 'butterchurn-presets' {
  // Import the Preset type from butterchurn
  import { Preset } from 'butterchurn';

  interface ButterchurnPresets {
    getPresets: () => Record<string, Preset>;
  }

  const butterchurnPresets: ButterchurnPresets;
  export default butterchurnPresets;
}