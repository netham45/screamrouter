/**
 * React component for visualizing audio streams.
 * It handles starting and stopping the visualization, resizing the canvas,
 * and managing the visualizer's lifecycle.
 */
import React, { useEffect, useState } from 'react';
import ApiService from '../api/api';

/**
 * Interface defining the props for the Visualizer component.
 */
interface VisualizerProps {
  ip: string;
}

/**
 * React functional component for rendering the audio stream visualizer.
 *
 * @param {VisualizerProps} props - The props passed to the component.
 * @returns {JSX.Element} The rendered JSX element.
 */
const Visualizer: React.FC<VisualizerProps> = ({ ip }) => {
  /**
   * State variable to indicate if the visualization has started.
   */
  const [started, setStarted] = useState(false);

  /**
   * Effect hook to handle document title and canvas resizing on component mount and unmount.
   */
  useEffect(() => {
    document.title = `ScreamRouter - Visualizer`;
    document.body.style.overflow = 'hidden';

    /**
     * Function to handle window resize events, adjusting the canvas size accordingly.
     */
    const handleResize = () => {
      const canvas = document.getElementById('canvas') as HTMLCanvasElement;
      if (canvas) {
        canvas.width = window.innerWidth;
        canvas.height = window.innerHeight;
        if ((window as any).visualizer) {
          (window as any).visualizer.setRendererSize(canvas.width, canvas.height);
        }
      }
    };

    window.addEventListener('resize', handleResize);
    handleResize(); // Initial size

    return () => {
      window.removeEventListener('resize', handleResize);
      if (window.stopVisualizer) {
        window.stopVisualizer();
      }
    };
  }, []);

  /**
   * Function to start the audio stream visualization.
   */
  const startVisualization = () => {
    const streamUrl = ApiService.getSinkStreamUrl(ip);
    if (window.startVisualizer) {
      window.startVisualizer(streamUrl);
      setStarted(true);
    }
  };

  /**
   * Render the Visualizer component with a button to start visualization and a canvas for rendering.
   */
  return (
    <div style={{ width: '100vw', height: '100vh', margin: 0, padding: 0, overflow: 'hidden', background: '#000' }}>
      {!started && (
        <button 
          onClick={startVisualization}
          style={{
            position: 'fixed',
            top: '50%',
            left: '50%',
            transform: 'translate(-50%, -50%)',
            padding: '20px 40px',
            fontSize: '18px',
            cursor: 'pointer',
            backgroundColor: '#007bff',
            color: 'white',
            border: 'none',
            borderRadius: '5px'
          }}
        >
          Start Visualization
        </button>
      )}
      <div id="mainWrapper" style={{ 
        position: 'fixed',
        top: 0,
        left: 0,
        width: '100vw',
        height: '100vh',
        display: started ? 'block' : 'none'
      }}>
        <canvas 
          id="canvas" 
          style={{ 
            width: '100%',
            height: '100%',
            display: 'block'
          }}
        ></canvas>
      </div>
      <audio id="audio_visualizer" style={{ display: 'none' }}></audio>
      <div id="presetControls" style={{ display: 'none' }}>
        <select id="presetSelect"></select>
      </div>
    </div>
  );
};

// Add window.startVisualizer type definition
declare global {
  interface Window {
    startVisualizer: (url: string) => void;
    stopVisualizer: () => void;
    visualizer: any;
  }
}

export default Visualizer;
