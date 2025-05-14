import React, { useState, useEffect, useCallback } from 'react';
import {
  Box, Flex, Text, Heading, Checkbox, Button, Input, Grid, GridItem,
  FormControl, FormLabel, Alert, AlertIcon, useColorModeValue, Tooltip, Select
} from '@chakra-ui/react';
import { useSearchParams } from 'react-router-dom';
import { useAppContext } from '../../context/AppContext';
import ApiService, { SpeakerLayout, Source, Sink, Route } from '../../api/api';
// Assuming SpeakerLayout type in api.ts is:
// export interface SpeakerLayout {
//   auto_mode: boolean;
//   matrix: number[][];
// }
// And Source, Sink, Route might now have:
// speaker_layouts?: { [key: number]: SpeakerLayout };
// channels?: number; // For Source
// speaker_layout is the old single one, which should be phased out from derivedItem if backend sends new structure.

// Local type alias to help with casting, assuming api.ts will be updated
type ItemWithOptionalSpeakerLayoutsAndChannels = (Source | Sink | Route) & {
  speaker_layouts?: { [key: number]: SpeakerLayout };
  channels?: number; // Relevant for Source and Sink
};

// Helper function to generate default matrix based on C++ logic from speaker_mix.cpp
// This should be placed within SpeakerLayoutPage.tsx or in a utility file and imported.
const generateCppDefaultMatrix = (inputChannels: number, outputChannels: number): number[][] => {
  const matrix = Array(8).fill(null).map(() => Array(8).fill(0.0)); // Start with a zero matrix

  // Mimic the C++ logic from speaker_mix.cpp
  // FL=0, FR=1, C=2, LFE=3, BL=4, BR=5, SL=6, SR=7
  switch (inputChannels) {
    case 1: // Mono input
      for (let i = 0; i < Math.min(outputChannels, 8); i++) {
        matrix[0][i] = 1.0; 
      }
      break;
    case 2: // Stereo input
      switch (outputChannels) {
        case 1: 
          matrix[0][0] = 0.5; matrix[1][0] = 0.5; 
          break;
        case 2: 
          matrix[0][0] = 1.0; matrix[1][1] = 1.0; 
          break;
        case 4: 
          matrix[0][0] = 1.0; matrix[0][2] = 1.0;
          matrix[1][1] = 1.0; matrix[1][3] = 1.0;
          break;
        case 6: 
          matrix[0][0] = 1.0;  matrix[0][5] = 1.0;  matrix[0][3] = 0.5;  matrix[0][4] = 0.5;
          matrix[1][1] = 1.0;  matrix[1][6] = 1.0;  matrix[1][3] = 0.5;  matrix[1][4] = 0.5;
          break;
        case 8: 
          matrix[0][0] = 1.0;  matrix[0][6] = 1.0;  matrix[0][4] = 1.0;
          matrix[0][2] = 0.5;  matrix[0][3] = 0.5;
          matrix[1][1] = 1.0;  matrix[1][7] = 1.0;  matrix[1][5] = 1.0;
          matrix[1][2] = 0.5;  matrix[1][3] = 0.5;
          break;
        default:
          break;
      }
      break;
    case 4: // Quadraphonic input (FL, FR, C, LFE as inputs 0,1,2,3)
      switch (outputChannels) {
        case 1: // Quad -> Mono
          matrix[0][0] = 0.25; matrix[1][0] = 0.25; matrix[2][0] = 0.25; matrix[3][0] = 0.25;
          break;
        case 2: // Quad -> Stereo
          matrix[0][0] = 0.5; matrix[1][1] = 0.5; matrix[2][0] = 0.5; matrix[3][1] = 0.5;
          // C++ comments imply inputs are FL,FR,RL,RR. If so, matrix[2] should be matrix[4], matrix[3] should be matrix[5]
          // Sticking to direct index mapping for now based on C++ array structure.
          break;
        case 4: // Quad -> Quad
          matrix[0][0] = 1.0; matrix[1][1] = 1.0; matrix[2][2] = 1.0; matrix[3][3] = 1.0;
          break;
        case 6: // Quad -> 5.1 Surround
          matrix[0][0] = 1.0;   matrix[1][1] = 1.0;
          matrix[0][2] = 0.5;  matrix[1][2] = 0.5;
          matrix[0][3] = 0.25; matrix[1][3] = 0.25; matrix[2][3] = 0.25; matrix[3][3] = 0.25;
          matrix[2][4] = 1.0;   matrix[3][5] = 1.0;
          break;
        case 8: // Quad -> 7.1 Surround
          matrix[0][0] = 1.0;   matrix[1][1] = 1.0;
          matrix[0][2] = 0.5;  matrix[1][2] = 0.5;
          matrix[0][3] = 0.25; matrix[1][3] = 0.25; matrix[2][3] = 0.25; matrix[3][3] = 0.25;
          matrix[2][4] = 1.0;   matrix[3][5] = 1.0;
          matrix[0][6] = 0.5;  matrix[1][7] = 0.5; matrix[2][6] = 0.5; matrix[3][7] = 0.5;
          break;
      }
      break;
    case 6: // 5.1 Surround input (FL,FR,C,LFE,BL,BR as inputs 0-5)
      switch (outputChannels) {
        case 1: // 5.1 -> Mono
          matrix[0][0] = 0.2; matrix[1][0] = 0.2; matrix[2][0] = 0.2; matrix[4][0] = 0.2; matrix[5][0] = 0.2; // Note: LFE (input 3) is skipped as per C++
          break;
        case 2: // 5.1 -> Stereo
          matrix[0][0] = 0.33; matrix[1][1] = 0.33;
          matrix[2][0] = 0.33; matrix[2][1] = 0.33;
          matrix[4][0] = 0.33; matrix[5][1] = 0.33;
          break;
        case 4: // 5.1 -> Quad
          matrix[0][0] = 0.66; matrix[1][1] = 0.66;
          matrix[2][0] = 0.33; matrix[2][1] = 0.33;
          matrix[4][2] = 1.0;   matrix[5][3] = 1.0;
          break;
        case 6: // 5.1 -> 5.1
          matrix[0][0]=1; matrix[1][1]=1; matrix[2][2]=1; matrix[3][3]=1; matrix[4][4]=1; matrix[5][5]=1;
          break;
        case 8: // 5.1 -> 7.1
          matrix[0][0]=1; matrix[1][1]=1; matrix[2][2]=1; matrix[3][3]=1; matrix[4][4]=1; matrix[5][5]=1;
          matrix[0][6]=0.5; matrix[1][7]=0.5; matrix[4][6]=0.5; matrix[5][7]=0.5;
          break;
      }
      break;
    case 8: // 7.1 Surround input (FL,FR,C,LFE,BL,BR,SL,SR as inputs 0-7)
      switch (outputChannels) {
        case 1: // 7.1 -> Mono
          matrix[0][0]=1/7; matrix[1][0]=1/7; matrix[2][0]=1/7; matrix[4][0]=1/7; matrix[5][0]=1/7; matrix[6][0]=1/7; matrix[7][0]=1/7; // Note: LFE (input 3) is skipped
          break;
        case 2: // 7.1 -> Stereo
          matrix[0][0]=0.5;   matrix[1][1]=0.5;
          matrix[2][0]=0.25;  matrix[2][1]=0.25;
          matrix[4][0]=0.125; matrix[5][1]=0.125;
          matrix[6][0]=0.125; matrix[7][1]=0.125;
          break;
        case 4: // 7.1 -> Quad
          matrix[0][0]=0.5;  matrix[1][1]=0.5;
          matrix[2][0]=0.25; matrix[2][1]=0.25;
          matrix[4][2]=0.66; matrix[5][3]=0.66;
          matrix[6][0]=0.25; matrix[7][1]=0.25;
          matrix[6][2]=0.33; matrix[7][3]=0.33;
          break;
        case 6: // 7.1 -> 5.1
          matrix[0][0]=0.66; matrix[1][1]=0.66;
          matrix[2][2]=1;   matrix[3][3]=1;
          matrix[4][4]=0.66; matrix[5][5]=0.66;
          matrix[6][0]=0.33; matrix[7][1]=0.33;
          matrix[6][4]=0.33; matrix[7][5]=0.33;
          break;
        case 8: // 7.1 -> 7.1
          matrix[0][0]=1; matrix[1][1]=1; matrix[2][2]=1; matrix[3][3]=1;
          matrix[4][4]=1; matrix[5][5]=1; matrix[6][6]=1; matrix[7][7]=1;
          break;
      }
      break;
    default:
      break;
  }
  return matrix;
};

// Map for channel counts to human-readable names
const channelOutputModeMap: { [key: number]: string } = {
  1: "Mono",
  2: "Stereo",
  4: "Quadraphonic",
  6: "5.1 Surround",
  8: "7.1 Surround",
};

const defaultMatrix = () => Array(8).fill(null).map((_, r) => 
  Array(8).fill(null).map((_, c) => (r === c ? 1.0 : 0.0)) // Store as 0.0-1.0
);

const SpeakerLayoutPage: React.FC = () => {
  const [searchParams] = useSearchParams();
  const { sources, sinks, routes, refreshAppContext } = useAppContext(); // Added refreshAppContext

  // Derive item and type from URL params and context on each render
  const typeParam = searchParams.get('type') as 'sources' | 'sinks' | 'routes' | null;
  const nameParam = searchParams.get('name');

  const [derivedItem, setDerivedItem] = useState<Source | Sink | Route | null>(null);
  const [initialPageError, setInitialPageError] = useState<string | null>(null);
  
  // New states for multi-layout management
  const [selectedInputChannelKey, setSelectedInputChannelKey] = useState<string>("2"); // Default to "2" (Stereo)
  const [itemSpeakerLayouts, setItemSpeakerLayouts] = useState<{ [key: number]: SpeakerLayout }>({});
  // currentLayout is now derived via useMemo below

  const [uiError, setUiError] = useState<string | null>(null);
  const [isLoading, setIsLoading] = useState(false);
  const [pageError, setPageError] = useState<string | null>(null);

  const inputChannelOptions = [
    { value: "1", label: "Mono Input (1ch)" },
    { value: "2", label: "Stereo Input (2ch)" },
    { value: "4", label: "Quad Input (4ch)" },
    { value: "6", label: "5.1 Input (6ch)" },
    { value: "8", label: "7.1 Input (8ch)" },
  ];

  // Effect to initialize and update item and layouts when URL params or context change
  useEffect(() => {
    let errorMsg: string | null = null;
    let item: Source | Sink | Route | null = null;
    if (!typeParam || !nameParam) {
      errorMsg = "Required URL parameters (type and name) are missing.";
    } else {
      const allItems = typeParam === 'sources' ? sources : typeParam === 'sinks' ? sinks : routes;
      item = allItems.find(i => i.name === nameParam) || null;

      if (!item) {
        const isAppContextLoading = sources.length === 0 && sinks.length === 0 && routes.length === 0;
        if (!isAppContextLoading) {
          errorMsg = `Item data not found for ${typeParam}: '${nameParam}'. Ensure the item exists.`;
        }
      }
    }
    setDerivedItem(item);
    setInitialPageError(errorMsg);
    setPageError(errorMsg);
  }, [typeParam, nameParam, sources, sinks, routes]);

  // Effect to initialize itemSpeakerLayouts from derivedItem (and handle page errors)
  useEffect(() => {
    if (derivedItem) {
      const layouts = (derivedItem as ItemWithOptionalSpeakerLayoutsAndChannels).speaker_layouts || {};
      setItemSpeakerLayouts(layouts);

      if (initialPageError && initialPageError.startsWith("Item data not found")) {
        setPageError(null);
      } else {
        setPageError(initialPageError);
      }
    } else {
      setItemSpeakerLayouts({}); // Clear if no derived item
      const isAppContextLoading = sources.length === 0 && sinks.length === 0 && routes.length === 0;
      if (!isAppContextLoading && !initialPageError) {
         setPageError(`Item data not found for ${typeParam}: '${nameParam}'.`);
      } else if (isAppContextLoading) {
        setPageError(null); 
      }
    }
  }, [derivedItem, initialPageError, sources, sinks, routes, typeParam, nameParam]);

  // currentLayout is now derived from itemSpeakerLayouts and selectedInputChannelKey
  const currentLayout: SpeakerLayout = React.useMemo(() => {
    const keyAsNumber = parseInt(selectedInputChannelKey, 10);
    const layout = itemSpeakerLayouts[keyAsNumber];
    return layout 
      ? { ...layout, matrix: layout.matrix || defaultMatrix() } 
      : { auto_mode: true, matrix: defaultMatrix() };
  }, [selectedInputChannelKey, itemSpeakerLayouts]);

  // REMOVED the other two useEffects that were syncing currentLayout and itemSpeakerLayouts.
  // Handlers will now update itemSpeakerLayouts directly.

  const handleLoadCppDefaultMatrix = useCallback(() => {
    if (!derivedItem) {
        setUiError("Cannot load default matrix: Item data is not available.");
        return;
    }
    if (typeParam !== 'sinks' && typeParam !== 'sources' && typeParam !== 'routes') {
        setUiError("Cannot load default matrix: Invalid item type for this operation.");
        return;
    }

    let outputChannels = 2;
    const itemWithChannels = derivedItem as ItemWithOptionalSpeakerLayoutsAndChannels;
    if (typeParam === 'sinks') {
        outputChannels = itemWithChannels.channels || 8; // Default to 8 for sinks if undefined
        if (!itemWithChannels.channels) {
            console.warn(`Sink "${derivedItem.name}" channels property is undefined. Defaulting to 8.`);
        }
    } else if (typeParam === 'sources' && itemWithChannels.channels) {
        outputChannels = itemWithChannels.channels;
    }

    const inputChKey = parseInt(selectedInputChannelKey, 10);
    if (isNaN(inputChKey)) {
        setUiError("Cannot load default matrix: Invalid input channel selection.");
        return;
    }

    const systemDefaultMatrix = generateCppDefaultMatrix(inputChKey, outputChannels);
    const newLayoutData = { auto_mode: false, matrix: systemDefaultMatrix };
    
    setItemSpeakerLayouts(prevLayouts => ({
      ...prevLayouts,
      [inputChKey]: newLayoutData,
    }));
    setUiError(null);
  }, [derivedItem, typeParam, selectedInputChannelKey, setItemSpeakerLayouts, setUiError]);

  const handleMatrixChange = (row: number, col: number, valueStr: string) => {
    const value = parseFloat(valueStr);
    const normalizedValue = Math.max(0, Math.min(100, Number.isNaN(value) ? 0 : value)) / 100;
    const keyToUpdate = parseInt(selectedInputChannelKey, 10);

    setItemSpeakerLayouts(prevLayouts => {
      const existingLayoutForKey = prevLayouts[keyToUpdate] || { auto_mode: true, matrix: defaultMatrix() };
      const matrixToUpdate = existingLayoutForKey.matrix && existingLayoutForKey.matrix.length === 8 
        ? existingLayoutForKey.matrix
        : defaultMatrix();
      const newMatrix = matrixToUpdate.map((r, rIndex) =>
        r.map((c, cIndex) => (rIndex === row && cIndex === col ? normalizedValue : c))
      );
      return {
        ...prevLayouts,
        [keyToUpdate]: { ...existingLayoutForKey, auto_mode: false, matrix: newMatrix },
      };
    });
  };

  const handleAutoModeChange = (event: React.ChangeEvent<HTMLInputElement>) => {
    const isChecked = event.target.checked;
    const keyToUpdate = parseInt(selectedInputChannelKey, 10);

    setItemSpeakerLayouts(prevLayouts => {
      const existingLayoutForKey = prevLayouts[keyToUpdate] || { auto_mode: !isChecked, matrix: defaultMatrix() };
      return {
        ...prevLayouts,
        [keyToUpdate]: {
          ...existingLayoutForKey,
          auto_mode: isChecked,
          matrix: isChecked ? defaultMatrix() : (existingLayoutForKey.matrix && existingLayoutForKey.matrix.length === 8 ? existingLayoutForKey.matrix : defaultMatrix()),
        },
      };
    });
  };

  const handleApply = async () => {
    if (!derivedItem || !typeParam) { // selectedInputChannelKey is not part of the main item update path
      setUiError("Cannot apply changes: Item data or type is missing.");
      return;
    }
    setIsLoading(true);
    setUiError(null);

    try {
      // Prepare the data payload containing only the speaker_layouts
      // This assumes the backend PUT /sinks/{name} (etc.) can handle this partial update.
      const payload = {
        speaker_layouts: itemSpeakerLayouts 
      };

      switch (typeParam) {
        case 'sources':
          await ApiService.updateSource(derivedItem.name, payload as Partial<Source>);
          break;
        case 'sinks':
          await ApiService.updateSink(derivedItem.name, payload as Partial<Sink>);
          break;
        case 'routes':
          await ApiService.updateRoute(derivedItem.name, payload as Partial<Route>);
          break;
        default:
          // This case should ideally not be reached if typeParam is validated earlier
          // or by TypeScript's type system, but as a fallback:
          setUiError(`Invalid item type for update: ${typeParam}`);
          setIsLoading(false);
          return;
      }
      
      if (typeof refreshAppContext === 'function') refreshAppContext();
      console.log("Speaker layouts applied via general update. App context refreshed.");

    } catch (err) {
      setUiError('Failed to apply speaker layouts using general update. Please try again.');
      console.error(err);
    } finally {
      setIsLoading(false);
    }
  };

  const handleResetCurrentToAuto = () => {
    const keyToUpdate = parseInt(selectedInputChannelKey, 10);
    setItemSpeakerLayouts(prevLayouts => ({
      ...prevLayouts,
      [keyToUpdate]: { auto_mode: true, matrix: defaultMatrix() },
    }));
  };
  
  const bgColor = useColorModeValue('white', 'gray.800');
  const inputBgColor = useColorModeValue('white', 'gray.600'); 
  const textColor = useColorModeValue('gray.800', 'white');
  const borderColor = useColorModeValue('gray.200', 'gray.700');

  const channelLabels = ["FL", "FR", "C", "LFE", "BL", "BR", "SL", "SR"];
  
  const isRealSink = typeParam === 'sinks' && derivedItem && !(derivedItem as Sink).is_group;
  const physicalOutputChannels = isRealSink ? (derivedItem as Sink).channels : 8; 

  if (pageError) {
    return (
      <Box p={5} bg={bgColor} borderRadius="lg" boxShadow="xl" color={textColor} maxW="600px" w="100%" m="auto" mt="10%">
        <Alert status="error" borderRadius="md"><AlertIcon />{pageError}</Alert>
        <Button onClick={() => window.close()} mt={4} colorScheme="blue">Close</Button>
      </Box>
    );
  }
  
  if (!derivedItem) {
    return (
      <Box p={5} bg={bgColor} borderRadius="lg" boxShadow="xl" color={textColor} maxW="600px" w="100%" m="auto" mt="10%">
        <Text>Loading speaker layout data for {nameParam || 'item'}...</Text>
      </Box>
    );
  }
  
  const displayMatrix = currentLayout.matrix && currentLayout.matrix.length === 8 ? currentLayout.matrix : defaultMatrix();
  const outputModeName = isRealSink ? channelOutputModeMap[(derivedItem as Sink).channels] || `${(derivedItem as Sink).channels}-channel` : null;

  return (
    <Box p={5} bg={bgColor} borderRadius="lg" boxShadow="xl" color={textColor} maxW="600px" w="100%" m="auto" mt={5}>
      <Heading size="md" mb={2} textAlign="center">Speaker Layout: {derivedItem.name}</Heading>
      
      {isRealSink && outputModeName && (
        <Text fontSize="sm" textAlign="center" mb={1} color="gray.500">
          Output Mode: {outputModeName} ({physicalOutputChannels} channels)
        </Text>
      )}

      <FormControl mb={4}>
        <FormLabel htmlFor="inputChannelKeySelect" fontWeight="semibold">Input Channel Configuration:</FormLabel>
        <Select 
          id="inputChannelKeySelect"
          value={selectedInputChannelKey}
          onChange={(e) => setSelectedInputChannelKey(e.target.value)}
          bg={inputBgColor}
          borderColor={borderColor}
        >
          {inputChannelOptions.map(opt => <option key={opt.value} value={opt.value}>{opt.label}</option>)}
        </Select>
      </FormControl>
      
      {uiError && <Alert status="error" mb={4} borderRadius="md"><AlertIcon />{uiError}</Alert>}

      <FormControl display="flex" alignItems="center" mb={4}>
        <FormLabel htmlFor="autoModeCheckbox" mb="0" mr={3} fontWeight="semibold">
          Auto Mode (for selected input):
        </FormLabel>
        <Checkbox
          id="autoModeCheckbox"
          isChecked={currentLayout.auto_mode}
          onChange={handleAutoModeChange}
          colorScheme="blue"
          size="lg"
        />
      </FormControl>

      <Box opacity={currentLayout.auto_mode ? 0.5 : 1} pointerEvents={currentLayout.auto_mode ? 'none' : 'auto'}>
        <Text mb={1} fontSize="sm" fontWeight="medium">
          Matrix for {inputChannelOptions.find(opt => opt.value === selectedInputChannelKey)?.label || "Selected Input"}:
        </Text>
        <Text mb={3} fontSize="xs" fontStyle="italic">(Values 0-100, where 100 = 1.0 gain)</Text>
        
        <Text textAlign="center" fontWeight="semibold" mb={1} fontSize="sm">Output Channels →</Text>

        <Grid templateColumns="50px repeat(8, 1fr)" gap={1} alignItems="center" mb={1}>
          <GridItem textAlign="center" display="flex" alignItems="center" justifyContent="center">
            <Text fontSize="xs" fontWeight="bold">Inputs</Text>
          </GridItem>
          {channelLabels.map((label, colIndex) => (
            <GridItem key={`col-label-${colIndex}`} textAlign="center" opacity={isRealSink && colIndex >= physicalOutputChannels ? 0.5 : 1}>
              <Tooltip label={`Output: ${label}`} placement="top" isDisabled={Boolean(isRealSink && colIndex >= physicalOutputChannels)}>
                <Text fontSize="xs" fontWeight="bold">{label.substring(0,2)}</Text>
              </Tooltip>
            </GridItem>
          ))}
        </Grid>
        
        {displayMatrix.map((row, rowIndex) => (
          <Grid templateColumns="50px repeat(8, 1fr)" gap={1} alignItems="center" key={`matrix-row-${rowIndex}`} mb={1}>
            <GridItem textAlign="center" display="flex" alignItems="center" justifyContent="center">
               <Tooltip label={`Input: ${channelLabels[rowIndex]}`} placement="left"> 
                <Text fontSize="xs" fontWeight="bold">{channelLabels[rowIndex].substring(0,2)}</Text>
              </Tooltip>
            </GridItem>
            {row.map((value, colIndex) => {
              const isDisabledByChannelCount = !!(isRealSink && colIndex >= physicalOutputChannels);
              return (
                <GridItem key={`cell-${rowIndex}-${colIndex}`}>
                  <Input
                    type="number"
                    value={Math.round((value || 0) * 100)} 
                    onChange={(e) => handleMatrixChange(rowIndex, colIndex, e.target.value)}
                    min={0}
                    max={100}
                    step={1}
                    size="sm"
                    textAlign="center"
                    isDisabled={currentLayout.auto_mode || isDisabledByChannelCount}
                    bg={isDisabledByChannelCount ? 'gray.600' : inputBgColor}
                    borderColor={borderColor}
                    _hover={{ borderColor: isDisabledByChannelCount ? borderColor : 'blue.400' }}
                    _focus={{ 
                        borderColor: isDisabledByChannelCount ? borderColor : 'blue.500', 
                        boxShadow: isDisabledByChannelCount ? 'none' : `0 0 0 1px var(--chakra-colors-blue-500)` 
                    }}
                    title={isDisabledByChannelCount ? `Output channel ${channelLabels[colIndex]} not available for this sink` : ""}
                  />
                </GridItem>
              );
            })}
          </Grid>
        ))}
      </Box>

      <Flex mt={6} justifyContent="space-between" alignItems="center">
        <Flex gap={2}>
            <Button onClick={handleResetCurrentToAuto} variant="outline" size="sm" isDisabled={isLoading || !derivedItem}>Reset Current to Auto</Button>
            {/* Changed "System Mix" to "Load C++ Default for Selected Input" */}
            <Button onClick={handleLoadCppDefaultMatrix} variant="outline" colorScheme="teal" size="sm" isDisabled={isLoading || !derivedItem || !selectedInputChannelKey}>
                Load Default
            </Button>
        </Flex>
        <Flex gap={3}>
          <Button onClick={() => window.close()} variant="ghost" size="sm" isDisabled={isLoading}>Close</Button>
          <Button colorScheme="blue" onClick={handleApply} isLoading={isLoading} size="sm" isDisabled={!derivedItem}>Apply All Layouts</Button>
        </Flex>
      </Flex>
    </Box>
  );
};

export default SpeakerLayoutPage;
