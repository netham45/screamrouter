/**
 * React component for visualizing audio streams.
 * It handles starting and stopping the visualization, resizing the canvas,
 * and managing the visualizer's lifecycle.
 * Uses Chakra UI components for consistent styling and butterchurn for visualization.
 */
import React, { useEffect, useState, useRef } from 'react';
import { Box, Button, Select, useColorModeValue, Flex, Text, IconButton } from '@chakra-ui/react';
import ApiService from '../../api/api';
import butterchurn from 'butterchurn';
import butterchurnPresets from 'butterchurn-presets';
import { FaPlay, FaStepBackward, FaStepForward } from 'react-icons/fa';
import { useAppContext } from '../../context/AppContext';

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
  // Get app context
  const { sources, activeSource, controlSource } = useAppContext();
  
  // State variables
  const [started, setStarted] = useState(false);
  const [isFullscreen, setIsFullscreen] = useState(false);
  const [presetIndex, setPresetIndex] = useState(0);
  const [presetRandom, setPresetRandom] = useState(true);
  const [presetCycle, setPresetCycle] = useState(true);
  const [presetCycleLength, setPresetCycleLength] = useState(15);
  const [showControls, setShowControls] = useState(false);
  const [showMediaControls, setShowMediaControls] = useState(true);
  
  // Define a type for the preset object
  type Preset = {
    baseVals: Record<string, number>;
    waves: Array<Record<string, number | string>>;
    init_eqs: Record<string, number>;
    [key: string]: unknown;
  };
  // Refs
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const audioRef = useRef<HTMLAudioElement>(null);
  const visualizerRef = useRef<ReturnType<typeof butterchurn.createVisualizer> | null>(null);
  const audioContextRef = useRef<AudioContext | null>(null);
  const sourceNodeRef = useRef<MediaElementAudioSourceNode | null>(null);
  const delayNodeRef = useRef<DelayNode | null>(null);
  const presetsRef = useRef<Record<string, Preset>>({});
  const presetKeysRef = useRef<string[]>([]);
  const presetIndexHistRef = useRef<number[]>([]);
  const cycleIntervalRef = useRef<NodeJS.Timeout | null>(null);
  const activePresetNameRef = useRef<string | null>(null);
  const mediaControlsTimeoutRef = useRef<NodeJS.Timeout | null>(null);
  
  /**
   * Effect hook to handle document title and canvas resizing on component mount and unmount.
   */
  useEffect(() => {
    document.title = `ScreamRouter - Visualizer`;
    document.body.style.overflow = 'hidden';
    
    // Load presets
    presetsRef.current = {
      ...butterchurnPresets.getPresets(),
    };
    
    // Sort presets by name
    presetsRef.current = Object.entries(presetsRef.current)
      .sort(([a], [b]) => a.toLowerCase().localeCompare(b.toLowerCase()))
      .reduce((acc, [key, value]) => ({ ...acc, [key]: value }), {});
    
    presetKeysRef.current = Object.keys(presetsRef.current);
    
    // Set initial preset index to random
    setPresetIndex(Math.floor(Math.random() * presetKeysRef.current.length));
    
    // Set initial canvas size with high resolution
    if (canvasRef.current) {
      // Use a higher resolution for better quality
      const pixelRatio = window.devicePixelRatio || 1;
      const width = window.innerWidth;
      const height = window.innerHeight;
      
      // Set the canvas size in CSS pixels
      canvasRef.current.style.width = `${width}px`;
      canvasRef.current.style.height = `${height}px`;
      
      // Set the canvas size in actual pixels (higher resolution)
      canvasRef.current.width = width * pixelRatio;
      canvasRef.current.height = height * pixelRatio;
    }
    
    // Handle resize events
    const handleResize = () => {
      if (canvasRef.current && visualizerRef.current) {
        const pixelRatio = window.devicePixelRatio || 1;
        const width = window.innerWidth;
        const height = window.innerHeight;
        
        // Update CSS size
        canvasRef.current.style.width = `${width}px`;
        canvasRef.current.style.height = `${height}px`;
        
        // Update actual size (higher resolution)
        canvasRef.current.width = width * pixelRatio;
        canvasRef.current.height = height * pixelRatio;
        
        // Recreate the visualizer with the new aspect ratio
        if (audioContextRef.current) {
          // Use the activePresetNameRef which is updated whenever a preset is loaded
          // This ensures we're using the actual current preset, not just what presetIndex points to
          console.log(`Resizing visualizer, maintaining preset: ${activePresetNameRef.current}`);
          
          // Make a deep copy of the preset to ensure it's not affected by any references
          const currentPreset = activePresetNameRef.current ?
            JSON.parse(JSON.stringify(presetsRef.current[activePresetNameRef.current])) : null;
          
          // Create new visualizer with updated dimensions
          visualizerRef.current = butterchurn.createVisualizer(
            audioContextRef.current,
            canvasRef.current,
            {
              width: canvasRef.current.width,
              height: canvasRef.current.height,
              pixelRatio,
              textureRatio: 2,
            }
          );
          
          // Reconnect audio
          if (delayNodeRef.current) {
            visualizerRef.current.connectAudio(delayNodeRef.current);
          }
          
          // Reload the current preset with zero blend time for immediate effect
          if (currentPreset) {
            visualizerRef.current.loadPreset(currentPreset, 0);
          }
        } else {
          // Just update the renderer size if we don't have an audio context yet
          visualizerRef.current.setRendererSize(canvasRef.current.width, canvasRef.current.height);
        }
      }
    };
    
    window.addEventListener('resize', handleResize);
    
    // Handle keyboard events
    const handleKeyDown = (e: KeyboardEvent) => {
      // Show media controls on any key press
      setShowMediaControls(true);
      
      // Reset the timeout for media controls
      if (mediaControlsTimeoutRef.current) {
        clearTimeout(mediaControlsTimeoutRef.current);
      }
      mediaControlsTimeoutRef.current = setTimeout(() => {
        setShowMediaControls(false);
      }, 2000);
      
      switch (e.key.toLowerCase()) {
        case 'f':
          toggleFullscreen();
          break;
        case 'n':
          nextPreset();
          break;
        case 'p':
          prevPreset();
          break;
        case 'r':
          randomPreset();
          break;
        case 'c':
          setShowControls(prev => !prev);
          break;
        case 'arrowleft':
          if (activeVncSource) {
            controlSource(activeVncSource.name, 'prevtrack');
          }
          break;
        case 'arrowright':
          if (activeVncSource) {
            controlSource(activeVncSource.name, 'nexttrack');
          }
          break;
        case ' ': // Space key
          if (activeVncSource) {
            controlSource(activeVncSource.name, 'play');
          }
          break;
      }
    };
    
    document.addEventListener('keydown', handleKeyDown);
    
    return () => {
      window.removeEventListener('resize', handleResize);
      document.removeEventListener('keydown', handleKeyDown);
      
      // Clean up audio context and nodes
      if (sourceNodeRef.current) {
        sourceNodeRef.current.disconnect();
      }
      
      if (delayNodeRef.current) {
        delayNodeRef.current.disconnect();
      }
      
      if (audioContextRef.current && audioContextRef.current.state !== 'closed') {
        audioContextRef.current.close();
      }
      
      // Clear cycle interval
      if (cycleIntervalRef.current) {
        clearInterval(cycleIntervalRef.current);
      }
      
      // Reset document title and overflow
      document.title = 'ScreamRouter';
      document.body.style.overflow = '';
    };
  }, []);
  
  /**
   * Effect to handle preset cycling
   */
  useEffect(() => {
    if (started && presetCycle) {
      if (cycleIntervalRef.current) {
        clearInterval(cycleIntervalRef.current);
      }
      
      cycleIntervalRef.current = setInterval(() => {
        if (presetRandom) {
          randomPreset();
        } else {
          nextPreset();
        }
      }, presetCycleLength * 1000);
      
      return () => {
        if (cycleIntervalRef.current) {
          clearInterval(cycleIntervalRef.current);
        }
      };
    }
  }, [started, presetCycle, presetRandom, presetCycleLength]);
  
  /**
   * Function to start the audio stream visualization.
   */
  const startVisualization = () => {
    const streamUrl = ApiService.getSinkStreamUrl(ip);
    
    if (!audioRef.current || !canvasRef.current) return;
    
    // Set up audio
    audioRef.current.src = streamUrl;
    audioRef.current.play().catch(error => {
      console.error('Error playing audio:', error);
    });
    
    // Set up visualizer
    audioContextRef.current = new AudioContext();
    
    // Create visualizer
    visualizerRef.current = butterchurn.createVisualizer(
      audioContextRef.current,
      canvasRef.current,
      {
        width: canvasRef.current.width,
        height: canvasRef.current.height,
        pixelRatio: window.devicePixelRatio || 1,
        textureRatio: 2, // Higher texture ratio for better quality
      }
    );
    
    // Connect audio to visualizer
    sourceNodeRef.current = audioContextRef.current.createMediaElementSource(audioRef.current);
    delayNodeRef.current = audioContextRef.current.createDelay();
    delayNodeRef.current.delayTime.value = 0.26; // Delay for better visualization
    
    sourceNodeRef.current.connect(delayNodeRef.current);
    visualizerRef.current.connectAudio(delayNodeRef.current);
    
    // Load initial preset
    const initialPresetName = presetKeysRef.current[presetIndex];
    activePresetNameRef.current = initialPresetName;
    loadPreset(presetIndex, 0);
    
    // Start rendering
    requestAnimationFrame(renderFrame);
    
    setStarted(true);
  };
  
  /**
   * Function to render a frame of the visualization.
   */
  const renderFrame = () => {
    if (visualizerRef.current) {
      visualizerRef.current.render();
    }
    requestAnimationFrame(renderFrame);
  };
  
  /**
   * Function to load a preset by index.
   */
  const loadPreset = (index: number, blendTime: number = 5.7) => {
    if (visualizerRef.current && presetKeysRef.current[index]) {
      const presetName = presetKeysRef.current[index];
      activePresetNameRef.current = presetName;
      console.log(`Loading preset: ${presetName}`);
      
      visualizerRef.current.loadPreset(
        presetsRef.current[presetName],
        blendTime
      );
    }
  };
  
  /**
   * Function to go to the next preset.
   */
  const nextPreset = () => {
    presetIndexHistRef.current.push(presetIndex);
    const newIndex = presetRandom
      ? Math.floor(Math.random() * presetKeysRef.current.length)
      : (presetIndex + 1) % presetKeysRef.current.length;
    
    setPresetIndex(newIndex);
    loadPreset(newIndex);
  };
  
  /**
   * Function to go to the previous preset.
   */
  const prevPreset = () => {
    const newIndex = presetIndexHistRef.current.length > 0
      ? presetIndexHistRef.current.pop() || 0
      : (presetIndex - 1 + presetKeysRef.current.length) % presetKeysRef.current.length;
    
    setPresetIndex(newIndex);
    loadPreset(newIndex);
  };
  
  /**
   * Function to load a random preset.
   */
  const randomPreset = () => {
    presetIndexHistRef.current.push(presetIndex);
    const newIndex = Math.floor(Math.random() * presetKeysRef.current.length);
    
    setPresetIndex(newIndex);
    loadPreset(newIndex);
  };
  
  /**
   * Function to toggle fullscreen mode.
   */
  const toggleFullscreen = () => {
    if (!isFullscreen) {
      const element = document.documentElement;
      if (element.requestFullscreen) {
        element.requestFullscreen().then(() => {
          // Force a resize after entering fullscreen to update aspect ratio
          setTimeout(() => {
            const resizeEvent = new Event('resize');
            window.dispatchEvent(resizeEvent);
          }, 100);
        }).catch(err => {
          console.error('Error attempting to enable fullscreen:', err);
        });
      }
    } else {
      if (document.exitFullscreen && document.fullscreenElement) {
        document.exitFullscreen().then(() => {
          // Force a resize after exiting fullscreen to update aspect ratio
          setTimeout(() => {
            const resizeEvent = new Event('resize');
            window.dispatchEvent(resizeEvent);
          }, 100);
        }).catch(err => {
          console.error('Error attempting to exit fullscreen:', err);
        });
      }
    }
    
    setIsFullscreen(!isFullscreen);
    setShowControls(false);
  };
  
  /**
   * Function to handle preset selection change.
   */
  const handlePresetChange = (e: React.ChangeEvent<HTMLSelectElement>) => {
    const newIndex = parseInt(e.target.value, 10);
    setPresetIndex(newIndex);
    loadPreset(newIndex);
  };
  
  /**
   * Function to handle preset cycle length change.
   */
  const handleCycleLengthChange = (e: React.ChangeEvent<HTMLInputElement>) => {
    setPresetCycleLength(parseInt(e.target.value, 10));
  };

  // Define colors based on color mode
  const buttonBg = useColorModeValue('blue.500', 'blue.400');
  const buttonHoverBg = useColorModeValue('blue.600', 'blue.500');
  const buttonColor = useColorModeValue('white', 'white');
  const controlsBg = useColorModeValue('rgba(255, 255, 255, 0.8)', 'rgba(0, 0, 0, 0.8)');
  // Check if there's an Primary Source with VNC set
  const activeVncSource = sources.find(source =>
    source.name === activeSource && source.vnc_ip && source.vnc_port
  );

  // Effect to handle mouse movement and hide media controls after inactivity
  useEffect(() => {
    if (!activeVncSource) return;
    
    let timeout: NodeJS.Timeout;
    
    const handleMouseMove = () => {
      // Show controls on mouse movement
      setShowMediaControls(true);
      
      // Clear any existing timeout
      clearTimeout(timeout);
      
      // Set a new timeout to hide controls after 2 seconds
      timeout = setTimeout(() => {
        setShowMediaControls(false);
      }, 2000);
    };
    
    // Media control keyboard handler
    const handleMediaKeys = (e: KeyboardEvent) => {
      console.log(`Key pressed: ${e.key}`);
      
      if (e.key === 'ArrowLeft') {
        console.log('Left arrow pressed - Previous track');
        controlSource(activeVncSource.name, 'prevtrack');
      } else if (e.key === 'ArrowRight') {
        console.log('Right arrow pressed - Next track');
        controlSource(activeVncSource.name, 'nexttrack');
      } else if (e.key === ' ') {
        console.log('Space pressed - Play/Pause');
        controlSource(activeVncSource.name, 'play');
      }
    };
    
    // Add mouse movement listener
    window.addEventListener('mousemove', handleMouseMove);
    
    // Add media control keyboard listener
    window.addEventListener('keydown', handleMediaKeys);
    
    // Initial timeout
    timeout = setTimeout(() => {
      setShowMediaControls(false);
    }, 2000);
    
    return () => {
      window.removeEventListener('mousemove', handleMouseMove);
      window.removeEventListener('keydown', handleMediaKeys);
      clearTimeout(timeout);
    };
  }, [activeVncSource, controlSource]);

  // Function to handle click on the background
  const handleBackgroundClick = () => {
    setShowControls(!showControls);
    setShowMediaControls(true);
    
    // Reset the timeout for media controls
    const timeout = setTimeout(() => {
      setShowMediaControls(false);
    }, 2000);
    
    return () => clearTimeout(timeout);
  };

  return (
    <Box
      width="100vw"
      height="100vh"
      margin={0}
      padding={0}
      overflow="hidden"
      bg="black"
      onClick={handleBackgroundClick}
    >
      {/* Media controls for VNC source */}
      {activeVncSource && showMediaControls && (
        <Flex
          position="fixed"
          bottom="20px"
          left="50%"
          transform="translateX(-50%)"
          bg="rgba(0, 0, 0, 0.7)"
          borderRadius="md"
          padding="10px"
          zIndex={1000}
          gap={4}
          onClick={(e) => e.stopPropagation()}
          opacity={1}
          transition="opacity 0.3s ease-in-out"
        >
          <IconButton
            aria-label="Previous Track"
            icon={<FaStepBackward />}
            onClick={() => controlSource(activeVncSource.name, 'prevtrack')}
            colorScheme="blue"
            size="lg"
          />
          <IconButton
            aria-label="Play/Pause"
            icon={<FaPlay />}
            onClick={() => controlSource(activeVncSource.name, 'play')}
            colorScheme="blue"
            size="lg"
          />
          <IconButton
            aria-label="Next Track"
            icon={<FaStepForward />}
            onClick={() => controlSource(activeVncSource.name, 'nexttrack')}
            colorScheme="blue"
            size="lg"
          />
        </Flex>
      )}
      {!started && (
        <Button
          onClick={startVisualization}
          position="fixed"
          top="50%"
          left="50%"
          transform="translate(-50%, -50%)"
          padding="20px 40px"
          fontSize="18px"
          bg={buttonBg}
          color={buttonColor}
          _hover={{ bg: buttonHoverBg }}
          borderRadius="md"
        >
          Start Visualization
        </Button>
      )}
      
      <Box
        as="canvas"
        ref={canvasRef}
        id="canvas"
        width={window.innerWidth}
        height={window.innerHeight}
        display="block"
      />
      
      <Box as="audio" ref={audioRef} id="audio_visualizer" display="none" />
      
      {started && showControls && showMediaControls && (
        <Box
          position="fixed"
          top="10px"
          right="10px"
          p={4}
          borderRadius="md"
          bg={controlsBg}
          zIndex={1000}
          onClick={(e) => e.stopPropagation()}
          opacity={1}
          transition="opacity 0.3s ease-in-out"
        >
          <Flex direction="column" gap={3}>
            <Flex align="center" justify="space-between">
              <Text fontWeight="bold">Preset:</Text>
              <Select
                value={presetIndex}
                onChange={handlePresetChange}
                width="250px"
                size="sm"
              >
                {presetKeysRef.current.map((name, index) => (
                  <option key={name} value={index}>
                    {name.length > 40 ? `${name.substring(0, 40)}...` : name}
                  </option>
                ))}
              </Select>
            </Flex>
            
            <Flex align="center" justify="space-between">
              <Text fontWeight="bold">Cycle Presets:</Text>
              <input
                type="checkbox"
                checked={presetCycle}
                onChange={() => setPresetCycle(!presetCycle)}
              />
            </Flex>
            
            <Flex align="center" justify="space-between">
              <Text fontWeight="bold">Cycle Length (seconds):</Text>
              <input
                type="number"
                value={presetCycleLength}
                onChange={handleCycleLengthChange}
                min={1}
                max={60}
                style={{ width: '60px' }}
              />
            </Flex>
            
            <Flex align="center" justify="space-between">
              <Text fontWeight="bold">Random Presets:</Text>
              <input
                type="checkbox"
                checked={presetRandom}
                onChange={() => setPresetRandom(!presetRandom)}
              />
            </Flex>
            
            <Flex justify="space-between" mt={2}>
              <Button size="sm" onClick={prevPreset}>Previous</Button>
              <Button size="sm" onClick={nextPreset}>Next</Button>
              <Button size="sm" onClick={randomPreset}>Random</Button>
            </Flex>
            
            <Button size="sm" onClick={toggleFullscreen} mt={2}>
              {isFullscreen ? 'Exit Fullscreen' : 'Fullscreen'}
            </Button>
            
            <Text fontSize="xs" mt={2}>
              Keyboard shortcuts: F (fullscreen), N (next), P (prev), R (random), C (controls)
            </Text>
          </Flex>
        </Box>
      )}
    </Box>
  );
};

export default Visualizer;

