/**
 * React component for the Equalizer.
 * This component provides a user interface to adjust equalizer settings for audio sources, sinks, or routes.
 * It includes sliders for each frequency band, preset selection, and buttons to apply changes, reset to default,
 * and close the window.
 * Uses Chakra UI components for consistent styling.
 *
 * @param {React.FC} props - The properties for the component.
 * @param {Object} props.item - The item (source, sink, or route) that the equalizer is being configured for.
 * @param {string} props.type - The type of item ('sources', 'sinks', 'routes', 'group-sink', 'group-source').
 * @param {Function} props.onClose - Function to call when closing the equalizer window.
 * @param {Function} props.onDataChange - Function to call when data changes (e.g., after applying settings).
 */
import React, { useState, useEffect, useRef } from 'react';
import {
  Box,
  Flex,
  Text,
  Heading,
  Select,
  Button,
  Slider,
  SliderTrack,
  SliderFilledTrack,
  SliderThumb,
  Alert,
  AlertIcon,
  useColorModeValue,
  Input,
  FormControl,
  FormLabel,
  Portal,
  Checkbox
} from '@chakra-ui/react';
import ApiService, { Equalizer as EqualizerType } from '../../api/api';

/**
 * Interface for the properties of the Equalizer component.
 */
interface EqualizerProps {
  item: {
    name: string;
    equalizer: EqualizerType;
  };
  type: 'sources' | 'sinks' | 'routes' | 'group-sink' | 'group-source';
  onClose: () => void;
  onDataChange: () => void;
}

/**
 * Default equalizer settings with all bands set to 1.
 */
const defaultEqualizer: EqualizerType = {
  b1: 1, b2: 1, b3: 1, b4: 1, b5: 1, b6: 1, b7: 1, b8: 1, b9: 1,
  b10: 1, b11: 1, b12: 1, b13: 1, b14: 1, b15: 1, b16: 1, b17: 1, b18: 1
};

/**
 * Preset equalizer settings for different music genres.
 */

const stockMusicPresets: { [key: string]: EqualizerType } = {
  '📦Flat': defaultEqualizer,
  '📦Classical': {
    b1: 1.2, b2: 1.2, b3: 1, b4: 1, b5: 1, b6: 1, b7: 0.9, b8: 0.9, b9: 0.9,
    b10: 0.8, b11: 0.8, b12: 0.8, b13: 0.8, b14: 0.8, b15: 0.8, b16: 0.8, b17: 0.8, b18: 0.8
  },
  '📦Rock': {
    b1: 1.2, b2: 1.1, b3: 0.8, b4: 0.9, b5: 1, b6: 1.2, b7: 1.4, b8: 1.4, b9: 1.4,
    b10: 1.3, b11: 1.2, b12: 1.1, b13: 1, b14: 1, b15: 1, b16: 1.1, b17: 1.2, b18: 1.2
  },
  '📦Pop': {
    b1: 0.8, b2: 0.9, b3: 1, b4: 1.1, b5: 1.2, b6: 1.2, b7: 1.1, b8: 1, b9: 1,
    b10: 1, b11: 1, b12: 1.1, b13: 1.2, b14: 1.2, b15: 1.1, b16: 1, b17: 0.9, b18: 0.8
  },
  '📦Jazz': {
    b1: 1.1, b2: 1.1, b3: 1, b4: 1, b5: 1, b6: 1.1, b7: 1.2, b8: 1.2, b9: 1.2,
    b10: 1.1, b11: 1, b12: 0.9, b13: 0.9, b14: 0.9, b15: 1, b16: 1.1, b17: 1.1, b18: 1.1
  },
  '📦Electronic': {
    b1: 1.4, b2: 1.3, b3: 1.2, b4: 1, b5: 0.8, b6: 1, b7: 1.2, b8: 1.3, b9: 1.4,
    b10: 1.4, b11: 1.3, b12: 1.2, b13: 1.1, b14: 1, b15: 1.1, b16: 1.2, b17: 1.3, b18: 1.4
  }
};


const musicPresets: { [key: string]: EqualizerType } = {...stockMusicPresets};

/**
 * Class representing a Biquad filter.
 */
class BiquadFilter {
    a0: number; a1: number; a2: number;
    b0: number; b1: number; b2: number;

    /**
     * Constructor for the BiquadFilter class.
     */
    constructor() {
      this.a0 = this.b0 = 1.0;
      this.a1 = this.a2 = this.b1 = this.b2 = 0.0;
    }

    /**
     * Sets the parameters of the filter.
     *
     * @param {number} freq - Frequency in Hz.
     * @param {number} Q - Quality factor.
     * @param {number} peakGain - Peak gain in dB.
     * @param {number} sampleRate - Sample rate in Hz.
     */
    setParams(freq: number, Q: number, peakGain: number, sampleRate: number) {
      const V = Math.pow(10, Math.abs(peakGain) / 20);
      const K = Math.tan(Math.PI * freq / sampleRate);

      if (peakGain >= 0) {    // boost
        const norm = 1 / (1 + 1/Q * K + K * K);
        this.b0 = (1 + V/Q * K + K * K) * norm;
        this.b1 = 2 * (K * K - 1) * norm;
        this.b2 = (1 - V/Q * K + K * K) * norm;
        this.a1 = this.b1;
        this.a2 = (1 - 1/Q * K + K * K) * norm;
      } else {    // cut
        const norm = 1 / (1 + V/Q * K + K * K);
        this.b0 = (1 + 1/Q * K + K * K) * norm;
        this.b1 = 2 * (K * K - 1) * norm;
        this.b2 = (1 - 1/Q * K + K * K) * norm;
        this.a1 = this.b1;
        this.a2 = (1 - V/Q * K + K * K) * norm;
      }
      this.a0 = 1.0;
    }
}

/**
 * React functional component for the Equalizer.
 *
 * @param {EqualizerProps} props - The properties for the component.
 */
const Equalizer: React.FC<EqualizerProps> = ({ item, type, onClose, onDataChange }) => {
  /**
   * State to keep track of the current equalizer settings.
   */
  const [equalizer, setEqualizer] = useState<EqualizerType>(item.equalizer);
  const [eqNormalization, setEqNormalization] = useState(item.equalizer.normalization_enabled ?? true);

  /**
   * State to keep track of any error messages.
   */
  const [error, setError] = useState<string | null>(null);

  /**
   * State to keep track of the selected preset.
   */
  const [preset, setPreset] = useState<string>('Custom');

  /**
   * State to keep track of custom equalizers.
   */
  const [customEqualizers, setCustomEqualizers] = useState<{ [key: string]: EqualizerType }>({});

  /**
   * State to control the save preset modal.
   */
  const [isSaveModalOpen, setIsSaveModalOpen] = useState(false);
  const [newPresetName, setNewPresetName] = useState('');

  /**
   * Reference to the canvas element for drawing the equalizer response graph.
   */
  const canvasRef = useRef<HTMLCanvasElement>(null);

  /**
   * Effect to load existing equalizers from the server and append them to musicPresets.
   */
  useEffect(() => {
    const loadEqualizers = async () => {
      try {
        const response = await ApiService.listEqualizers();
        const equalizers = response.data['equalizers'];
        const customEq: { [key: string]: EqualizerType } = {};
        equalizers.forEach(eq => {
          if (eq.name == undefined)
            return;
          const name = `🛠️${eq.name}`;
          customEq[name] = eq;
          musicPresets[name] = eq;
        });
        setCustomEqualizers(customEq);
        checkAndSetPreset(item.equalizer);
        console.log(musicPresets);
      } catch (error) {
        console.error('Error loading equalizers:', error);
        setError('Failed to load equalizers. Please try again.');
      }
      console.log(musicPresets);
    };

    loadEqualizers();
  }, []);

  /**
   * Checks if the given equalizer settings match any of the presets and sets the preset state accordingly.
   *
   * @param {EqualizerType} eq - The equalizer settings to check.
   */
  const checkAndSetPreset = (eq: EqualizerType) => {
    // eslint-disable-next-line @typescript-eslint/no-unused-vars
    const matchingPreset = Object.entries({ ...musicPresets, ...customEqualizers }).find(([_, presetEq]) =>
      Object.entries(presetEq).every(([band, value]) => {
        if (band === "name") return true;
        const eqValue = eq[band as keyof EqualizerType];
        return typeof eqValue === 'number' && Math.abs(eqValue - value) < 0.01;
      })
    );

    if (matchingPreset) {
      setPreset(matchingPreset[0]);
    } else {
      setPreset('Custom');
    }
  };

  /**
   * Updates the equalizer settings on the server.
   */
  const updateEqualizer = async () => {
    try {
      setError(null);
      const eqData = { ...equalizer, normalization_enabled: eqNormalization };
      switch (type) {
        case 'sources':
          await ApiService.updateSourceEqualizer(item.name, eqData);
          break;
        case 'sinks':
          await ApiService.updateSinkEqualizer(item.name, eqData);
          break;
        case 'routes':
          await ApiService.updateRouteEqualizer(item.name, eqData);
          break;
      }
    } catch (error) {
      console.error('Error updating equalizer:', error);
      setError('Failed to update equalizer. Please try again.');
    }
  };

  /**
   * Handles changes to the equalizer sliders.
   *
   * @param {keyof EqualizerType} band - The frequency band that was changed.
   * @param {number} value - The new value for the frequency band.
   */
  const handleChange = (band: keyof EqualizerType, value: number) => {
    setEqualizer(prev => {
      const newEqualizer = { ...prev, [band]: value };
      checkAndSetPreset(newEqualizer);
      return newEqualizer;
    });
  };

  /**
   * Handles changes to the preset selection dropdown.
   *
   * @param {React.ChangeEvent<HTMLSelectElement>} event - The change event from the select element.
   */
  const handlePresetChange = (event: React.ChangeEvent<HTMLSelectElement>) => {
    const selectedPreset = event.target.value;
    setPreset(selectedPreset);
    if (selectedPreset !== 'Custom') {
      setEqualizer({ ...musicPresets[selectedPreset], ...customEqualizers[selectedPreset] });
    }
  };

  /**
   * Closes the equalizer window and triggers a data reload.
   */
  const handleClose = () => {
    onClose();
    onDataChange(); // Trigger data reload when the equalizer window is closed
  };

  /**
   * Opens the save preset modal.
   */
  const openSaveModal = () => {
    setNewPresetName('');
    setIsSaveModalOpen(true);
  };

  /**
   * Closes the save preset modal.
   */
  const closeSaveModal = () => {
    setIsSaveModalOpen(false);
  };

  /**
   * Saves a new equalizer preset.
   */
  const saveEqualizer = async () => {
    if (!newPresetName) {
      setError('Name is required.');
      return;
    }
    if (musicPresets[newPresetName] || customEqualizers[newPresetName]) {
      setError('Preset name already exists.');
      return;
    }
    try {
      setError(null);
      await ApiService.saveEqualizer(newPresetName, equalizer);
      setCustomEqualizers(prev => ({ ...prev, [`🛠️${newPresetName}`]: equalizer }));
      setPreset(`🛠️${newPresetName}`);
      closeSaveModal();
    } catch (error) {
      console.error('Error saving equalizer:', error);
      setError('Failed to save equalizer. Please try again.');
    }
  };

  /**
   * Deletes a custom equalizer preset.
   */
  const deleteEqualizer = async () => {
    if (preset in stockMusicPresets) {
      setError('Cannot delete a hard-coded preset.');
      return;
    }
    try {
      setError(null);
      await ApiService.deleteEqualizer(preset.replace("🛠️",""));
      setCustomEqualizers(prev => {
        const newCustomEq = { ...prev };
        delete newCustomEq[preset];
        return newCustomEq;
      });
      setPreset('Custom');
    } catch (error) {
      console.error('Error deleting equalizer:', error);
      setError('Failed to delete equalizer. Please try again.');
    }
  };

  /**
   * Sorted list of frequency bands for rendering sliders in order.
   */
  const sortedBands = Object.entries(equalizer).filter(([key]) => key.startsWith('b')).sort((a, b) => {
    const bandA = parseInt(a[0].slice(1), 10);
    const bandB = parseInt(b[0].slice(1), 10);
    return bandA - bandB;
  });

  /**
   * Frequencies corresponding to each equalizer band.
   */
  const frequencies = [65.406392, 92.498606, 130.81278, 184.99721, 261.62557, 369.99442, 523.25113, 739.9884,
                       1046.5023, 1479.9768, 2093.0045, 2959.9536, 4186.0091, 5919.9072, 8372.0181, 11839.814,
                       16744.036, 20000.0];

  /**
   * Calculates the magnitude response of the equalizer at a given frequency.
   *
   * @param {number} freq - The frequency in Hz.
   * @param {number} sampleRate - The sample rate in Hz.
   * @returns {number} The magnitude response in dB.
   */
  const calculateMagnitudeResponse = (freq: number, sampleRate: number): number => {
    let totalGain = 1;
    // eslint-disable-next-line @typescript-eslint/no-unused-vars
    sortedBands.forEach(([band, gain], index) => {
      console.log(band);
      const filter = new BiquadFilter();
      filter.setParams(frequencies[index], 1.41, 20 * Math.log10(Math.max(gain, 0.001)), sampleRate);

      const w = 2 * Math.PI * freq / sampleRate;
      const phi = Math.pow(Math.sin(w/2), 2);
      const b0 = filter.b0, b1 = filter.b1, b2 = filter.b2, a0 = filter.a0, a1 = filter.a1, a2 = filter.a2;

      const magnitude = Math.sqrt(
        Math.pow(b0 + b1 + b2, 2) - 4*(b0*b1 + 4*b0*b2 + b1*b2)*phi + 16*b0*b2*phi*phi
      ) / Math.sqrt(
        Math.pow(a0 + a1 + a2, 2) - 4*(a0*a1 + 4*a0*a2 + a1*a2)*phi + 16*a0*a2*phi*phi
      );

      totalGain *= magnitude;
    });

    return 20 * Math.log10(totalGain);
  };

  /**
   * Effect to draw the equalizer response graph on the canvas.
   */
  useEffect(() => {
    const canvas = canvasRef.current;
    if (canvas) {
      const ctx = canvas.getContext('2d');
      if (ctx) {
        ctx.clearRect(0, 0, canvas.width, canvas.height);

        // Set canvas background to dark
        ctx.fillStyle = '#1e1e1e';
        ctx.fillRect(0, 0, canvas.width, canvas.height);

        // Draw background grid
        ctx.strokeStyle = 'rgba(255, 255, 255, 0.1)';
        ctx.lineWidth = 1;

        // Vertical grid lines (frequency)
        const freqLabels = [20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000, 24000];
        freqLabels.forEach(freq => {
          const x = (Math.log2(freq / 20)) / (Math.log2(24000 / 20)) * canvas.width;
          ctx.beginPath();
          ctx.moveTo(x, 0);
          ctx.lineTo(x, canvas.height - 20);
          ctx.stroke();

          // Frequency labels
          ctx.fillStyle = 'rgba(255, 255, 255, 0.7)';
          ctx.font = '10px Arial';
          ctx.textAlign = 'center';
          ctx.fillText(freq >= 1000 ? `${freq/1000}k` : freq.toString(), x, canvas.height - 5);
        });

        // Horizontal grid lines (dB)
        const dbLabels = [-12, -9, -6, -3, 0, 3, 6, 9, 12];
        const maxDb = 12;
        dbLabels.forEach(db => {
          const y = canvas.height / 2 - 10 - (db / maxDb) * ((canvas.height - 20) / 2);
          ctx.beginPath();
          ctx.moveTo(0, y);
          ctx.lineTo(canvas.width, y);
          ctx.stroke();

          // dB labels
          ctx.fillStyle = 'rgba(255, 255, 255, 0.7)';
          ctx.font = '10px Arial';
          ctx.textAlign = 'right';
          ctx.fillText(`${db} dB`, canvas.width - 5, y + 3);
        });

        // Draw equalizer response
        ctx.strokeStyle = '#2ecc71';
        ctx.lineWidth = 2;
        ctx.beginPath();

        const sampleRate = 48000;
        const points = 200;

        for (let i = 0; i < points; i++) {
          const freq = Math.exp(Math.log(20) + (Math.log(24000) - Math.log(20)) * (i / (points - 1)));
          const magnitude = calculateMagnitudeResponse(freq, sampleRate);

          const x = (Math.log2(freq / 20)) / (Math.log2(24000 / 20)) * canvas.width;
          const y = canvas.height / 2 - 10 - (magnitude / maxDb) * ((canvas.height - 20) / 2);

          if (i === 0) {
            ctx.moveTo(x, y);
          } else {
            ctx.lineTo(x, y);
          }
        }
        ctx.stroke();

        // X-axis label
        ctx.fillStyle = 'rgba(255, 255, 255, 0.9)';
        ctx.font = '12px Arial';
        ctx.textAlign = 'center';
        ctx.fillText('Frequency (Hz)', canvas.width / 2, canvas.height - 20);

        // Y-axis label
        ctx.save();
        ctx.translate(10, canvas.height / 2);
        ctx.rotate(-Math.PI / 2);
        ctx.textAlign = 'center';
        ctx.fillText('Gain (dB)', 0, 0);
        ctx.restore();
      }
    }
  }, [equalizer]);

  /**
   * Effect to clear the error message after 10 seconds if it is set.
   */
  useEffect(() => {
    let timer: NodeJS.Timeout | null = null;

    if (error) {
      timer = setTimeout(() => {
        setError(null);
      }, 10000);
    }

    return () => {
      if (timer) {
        clearTimeout(timer);
      }
    };
  }, [error]);

  // Color values for light/dark mode
  const bgColor = useColorModeValue('white', 'gray.800');
  const borderColor = useColorModeValue('gray.200', 'gray.700');
  const textColor = useColorModeValue('gray.800', 'white');
  const canvasBorderColor = useColorModeValue('gray.300', 'gray.600');
  const sliderBgColor = useColorModeValue('blue.100', 'blue.900');
  const sliderFilledColor = useColorModeValue('blue.500', 'blue.300');
  const sliderThumbColor = useColorModeValue('blue.600', 'blue.400');

  /**
   * Renders the Equalizer component.
   *
   * @returns {JSX.Element} The rendered JSX element.
   */
  return (
    <Box
      p={4}
      bg={bgColor}
      borderWidth="1px"
      borderColor={borderColor}
      borderRadius="md"
      maxW="800px"
      mx="auto"
    >
      <Heading as="h3" size="md" mb={4} textAlign="center">
        {item.name} {type.replace(/s$/,"").replace(/^(.)/, function(m){return m.toUpperCase()})} Equalizer
      </Heading>
      
      {error && (
        <Alert status="error" mb={4} borderRadius="md">
          <AlertIcon />
          {error}
        </Alert>
      )}
      
      <Box mb={4} textAlign="center">
        <Box
          as="canvas"
          ref={canvasRef}
          width="600"
          height="310"
          borderWidth="1px"
          borderColor={canvasBorderColor}
          borderRadius="md"
          mx="auto"
        />
      </Box>
      
      <Box mb={4}>
        <Flex mb={4} alignItems="center" justifyContent="center" gap={2}>
          <Text fontWeight="medium" mr={2}>Preset:</Text>
          <Select
            value={preset}
            onChange={handlePresetChange}
            width="auto"
            minW="200px"
          >
            <option value="Custom">Custom</option>
            {Object.keys({ ...musicPresets, ...customEqualizers }).map(presetName => (
              <option key={presetName} value={presetName}>{presetName}</option>
            ))}
          </Select>
          <Button colorScheme="blue" size="sm" onClick={openSaveModal} ml={2}>
            Save
          </Button>
          <Button colorScheme="red" size="sm" onClick={deleteEqualizer}>
            Delete
          </Button>
          <Checkbox
               isChecked={eqNormalization}
               onChange={(e) => setEqNormalization(e.target.checked)}
               ml={4}
             >
               Normalize
             </Checkbox>
        </Flex>
       
        <Flex
          justifyContent="space-between"
          alignItems="flex-end"
          height="200px"
          position="relative"
          px={4}
        >
          {sortedBands.map(([band, value], index) => (
            <Flex
              key={band}
              direction="column"
              alignItems="center"
              height="100%"
              width={`${100 / sortedBands.length}%`}
            >
              <Slider
                id={band}
                orientation="vertical"
                min={0}
                max={2}
                step={0.1}
                value={value}
                onChange={(val) => handleChange(band as keyof EqualizerType, val)}
                height="180px"
              >
                <SliderTrack bg={sliderBgColor} width="8px">
                  <SliderFilledTrack bg={sliderFilledColor} />
                </SliderTrack>
                <SliderThumb bg={sliderThumbColor} boxSize="16px" />
              </Slider>
              <Text fontSize="xs" mt={2} color={textColor}>
                {frequencies[index].toFixed(0)}
              </Text>
            </Flex>
          ))}
          <Text
            position="absolute"
            bottom="-25px"
            right="0"
            fontSize="sm"
            color={textColor}
          >
            Hz
          </Text>
        </Flex>
      </Box>
      
      <Flex justifyContent="center" gap={4} mt={6}>
        <Button colorScheme="green" onClick={updateEqualizer}>
          Apply
        </Button>
        <Button variant="outline" onClick={handleClose}>
          Close
        </Button>
      </Flex>
      {/* Save Preset Modal - Using Portal to ensure it's at the root level */}
      <Portal>
        {isSaveModalOpen && (
          <Box
            position="fixed"
            top="0"
            left="0"
            right="0"
            bottom="0"
            bg="rgba(0, 0, 0, 0.5)"
            zIndex="9000"
            display="flex"
            alignItems="center"
            justifyContent="center"
            onClick={closeSaveModal}
          >
            <Box
              bg={bgColor}
              borderRadius="md"
              maxW="400px"
              w="90%"
              p={4}
              boxShadow="lg"
              onClick={(e) => e.stopPropagation()}
              zIndex="9999"
            >
              <Heading as="h3" size="md" mb={4}>
                Save Equalizer Preset
              </Heading>
              <FormControl mb={4}>
                <FormLabel>Preset Name</FormLabel>
                <Input 
                  placeholder="Enter a name for your preset"
                  value={newPresetName}
                  onChange={(e) => setNewPresetName(e.target.value)}
                  autoFocus
                />
              </FormControl>
              <Flex justifyContent="flex-end">
                <Button colorScheme="blue" mr={3} onClick={saveEqualizer}>
                  Save
                </Button>
                <Button onClick={closeSaveModal}>
                  Cancel
                </Button>
              </Flex>
            </Box>
          </Box>
        )}
      </Portal>
    </Box>
  );
};

export default Equalizer;
