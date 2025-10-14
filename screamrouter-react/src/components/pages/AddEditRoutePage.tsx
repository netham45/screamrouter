/**
 * React component for adding or editing a route in a standalone page.
 * This component provides a form to input details about a route, including its name, source, sink,
 * volume, delay, and timeshift settings.
 * It allows the user to either add a new route or update an existing one.
 */
import React, { useState, useEffect } from 'react';
import { useSearchParams } from 'react-router-dom';
import {
  Flex,
  FormControl,
  FormLabel,
  Input,
  Select,
  Button,
  Stack,
  NumberInput,
  NumberInputField,
  NumberInputStepper,
  NumberIncrementStepper,
  NumberDecrementStepper,
  Alert,
  AlertIcon,
  Box,
  Heading,
  Container,
  useColorModeValue,
  Switch
} from '@chakra-ui/react';
import ApiService, { Route, Source, Sink } from '../../api/api';
import VolumeSlider from './controls/VolumeSlider';
import TimeshiftSlider from './controls/TimeshiftSlider';

const AddEditRoutePage: React.FC = () => {
  const [searchParams] = useSearchParams();
  const routeName = searchParams.get('name');
  const preselectedSource = searchParams.get('source');
  const preselectedSink = searchParams.get('sink');
  const isEdit = !!routeName;

  // eslint-disable-next-line @typescript-eslint/no-unused-vars
  const [route, setRoute] = useState<Route | null>(null);
  const [name, setName] = useState('');
  const [nameManuallyEdited, setNameManuallyEdited] = useState(false);
  const [source, setSource] = useState('');
  const [sink, setSink] = useState('');
  const [enabled, setEnabled] = useState(true);
  const [volume, setVolume] = useState(1);
  const [delay, setDelay] = useState(0);
  const [timeshift, setTimeshift] = useState(0);
  
  const [sources, setSources] = useState<Source[]>([]);
  const [sinks, setSinks] = useState<Sink[]>([]);
  const [error, setError] = useState<string | null>(null);
  const [success, setSuccess] = useState<string | null>(null);

  // Color values for light/dark mode
  const bgColor = useColorModeValue('white', 'gray.800');
  const borderColor = useColorModeValue('gray.200', 'gray.700');
  const inputBg = useColorModeValue('white', 'gray.700');

  // Fetch sources and sinks
  useEffect(() => {
    const fetchData = async () => {
      try {
        const [sourcesResponse, sinksResponse] = await Promise.all([
          ApiService.getSources(),
          ApiService.getSinks()
        ]);
        
        setSources(Object.values(sourcesResponse.data));
        setSinks(Object.values(sinksResponse.data));
        
        // Set preselected source and sink if provided in URL
        if (preselectedSource && !isEdit) {
          const sourceExists = Object.values(sourcesResponse.data).some(
            s => s.name === preselectedSource
          );
          if (sourceExists) {
            setSource(preselectedSource);
          }
        }
        
        if (preselectedSink && !isEdit) {
          const sinkExists = Object.values(sinksResponse.data).some(
            s => s.name === preselectedSink
          );
          if (sinkExists) {
            setSink(preselectedSink);
          }
        }
      } catch (error) {
        console.error('Error fetching sources and sinks:', error);
        setError('Failed to fetch sources and sinks. Please try again.');
      }
    };

    fetchData();
  }, [preselectedSource, preselectedSink, isEdit]);

  // Fetch route data if editing
  useEffect(() => {
    const fetchRoute = async () => {
      if (routeName) {
        try {
          const response = await ApiService.getRoutes();
          const routeData = Object.values(response.data).find(r => r.name === routeName);
          if (routeData) {
            setRoute(routeData);
            setName(routeData.name);
            setSource(routeData.source);
            setSink(routeData.sink);
            setEnabled(routeData.enabled);
            setVolume(routeData.volume || 1);
            setDelay(routeData.delay || 0);
            setTimeshift(routeData.timeshift || 0);
          } else {
            setError(`Route "${routeName}" not found.`);
          }
        } catch (error) {
          console.error('Error fetching route:', error);
          setError('Failed to fetch route data. Please try again.');
        }
      }
    };

    fetchRoute();
  }, [routeName]);
  
  // Auto-fill name when source and sink are selected
  useEffect(() => {
    // Only auto-fill name if we're not in edit mode and the name hasn't been manually edited
    if (!isEdit && !nameManuallyEdited && source && sink) {
      setName(`${source} to ${sink}`);
    }
  }, [source, sink, nameManuallyEdited, isEdit]);

  /**
   * Handles form submission to add or update a route.
   * Validates input and sends the data to the API service.
   */
  const handleSubmit = async () => {
    if (!source) {
      setError('Please select a source.');
      return;
    }

    if (!sink) {
      setError('Please select a sink.');
      return;
    }

    const routeData: Partial<Route> = {
      name,
      source,
      sink,
      enabled,
      volume,
      delay,
      timeshift
    };

    try {
      if (isEdit) {
        await ApiService.updateRoute(routeName!, routeData);
        setSuccess(`Route "${name}" updated successfully.`);
      } else {
        await ApiService.addRoute(routeData as Route);
        setSuccess(`Route "${name}" added successfully.`);
      }
      
      // Clear form if adding a new route
      if (!isEdit) {
        setName('');
        setSource('');
        setSink('');
        setEnabled(true);
        setVolume(1);
        setDelay(0);
        setTimeshift(0);
      }
    } catch (error) {
      console.error('Error submitting route:', error);
      setError('Failed to submit route. Please try again.');
    }
  };

  const handleClose = () => {
    window.close();
  };

  return (
    <Container maxW="container.md" py={8}>
      <Box
        bg={bgColor}
        borderColor={borderColor}
        borderWidth="1px"
        borderRadius="lg"
        p={6}
        boxShadow="md"
      >
        <Heading as="h1" size="lg" mb={6}>
          {isEdit ? 'Edit Route' : 'Add Route'}
        </Heading>
        
        {error && (
          <Alert status="error" mb={4} borderRadius="md">
            <AlertIcon />
            {error}
          </Alert>
        )}
        
        {success && (
          <Alert status="success" mb={4} borderRadius="md">
            <AlertIcon />
            {success}
          </Alert>
        )}
        
        <Stack spacing={4}>
          <FormControl isRequired>
            <FormLabel>Route Name</FormLabel>
            <Input
              value={name}
              onChange={(e) => {
                setName(e.target.value);
                setNameManuallyEdited(e.target.value !== "");
              }}
              bg={inputBg}
            />
          </FormControl>
          
          <FormControl isRequired>
            <FormLabel>Source</FormLabel>
            <Select
              value={source}
              onChange={(e) => setSource(e.target.value)}
              bg={inputBg}
              placeholder="Select a source"
            >
              {sources.map(src => (
                <option key={src.name} value={src.name}>
                  {src.name}
                </option>
              ))}
            </Select>
          </FormControl>
          
          <FormControl isRequired>
            <FormLabel>Sink</FormLabel>
            <Select
              value={sink}
              onChange={(e) => setSink(e.target.value)}
              bg={inputBg}
              placeholder="Select a sink"
            >
              {sinks.map(s => (
                <option key={s.name} value={s.name}>
                  {s.name}
                </option>
              ))}
            </Select>
          </FormControl>
          
          <FormControl display="flex" alignItems="center">
            <FormLabel htmlFor="enabled" mb="0">
              Enabled
            </FormLabel>
            <Switch 
              id="enabled" 
              isChecked={enabled}
              onChange={(e) => setEnabled(e.target.checked)}
            />
          </FormControl>
          
          <FormControl>
            <FormLabel>Volume</FormLabel>
            <VolumeSlider value={volume} onChange={setVolume} />
          </FormControl>
          
          <FormControl>
            <FormLabel>Delay (ms)</FormLabel>
            <NumberInput
              value={delay}
              onChange={(valueString) => setDelay(parseInt(valueString) || 0)}
              min={0}
              max={5000}
              bg={inputBg}
            >
              <NumberInputField />
              <NumberInputStepper>
                <NumberIncrementStepper />
                <NumberDecrementStepper />
              </NumberInputStepper>
            </NumberInput>
          </FormControl>
          
          <FormControl>
            <FormLabel>Timeshift</FormLabel>
            <TimeshiftSlider value={timeshift} onChange={setTimeshift} />
          </FormControl>
        </Stack>
        
        <Flex mt={8} gap={3} justifyContent="flex-end">
          <Button colorScheme="blue" onClick={handleSubmit}>
            {isEdit ? 'Update Route' : 'Add Route'}
          </Button>
          <Button variant="outline" onClick={handleClose}>
            Close
          </Button>
        </Flex>
      </Box>
    </Container>
  );
};

export default AddEditRoutePage;