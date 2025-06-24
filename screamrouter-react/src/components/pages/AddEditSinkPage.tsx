/**
 * React component for adding or editing a sink in a standalone page.
 * This component provides a form to input details about a sink, including its name, IP address,
 * port, bit depth, sample rate, channels, channel layout, volume, delay, time sync settings, and more.
 * It allows the user to either add a new sink or update an existing one.
 */
import React, { useState, useEffect } from 'react';
import { useSearchParams } from 'react-router-dom';
import {
  Flex,
  FormControl,
  FormLabel,
  Input,
  Select,
  Checkbox,
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
  useColorModeValue
} from '@chakra-ui/react';
import ApiService, { Sink } from '../../api/api';
import VolumeSlider from './controls/VolumeSlider';
import TimeshiftSlider from './controls/TimeshiftSlider';

const AddEditSinkPage: React.FC = () => {
  const [searchParams] = useSearchParams();
  const sinkName = searchParams.get('name');
  const isEdit = !!sinkName;

  // eslint-disable-next-line @typescript-eslint/no-unused-vars
  const [sink, setSink] = useState<Sink | null>(null);
  const [name, setName] = useState('');
  const [ip, setIp] = useState('');
  const [port, setPort] = useState('4010');
  const [bitDepth, setBitDepth] = useState('32');
  const [sampleRate, setSampleRate] = useState('48000');
  const [channels, setChannels] = useState('2');
  const [channelLayout, setChannelLayout] = useState('stereo');
  const [volume, setVolume] = useState(1);
  const [delay, setDelay] = useState(0);
  const [timeSync, setTimeSync] = useState(false);
  const [timeSyncDelay, setTimeSyncDelay] = useState('0');
  const [protocol, setProtocol] = useState('scream');
  const [volumeNormalization, setVolumeNormalization] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [success, setSuccess] = useState<string | null>(null);

  // Color values for light/dark mode
  const bgColor = useColorModeValue('white', 'gray.800');
  const borderColor = useColorModeValue('gray.200', 'gray.700');
  const inputBg = useColorModeValue('white', 'gray.700');

  // Fetch sink data if editing
  useEffect(() => {
    const fetchSink = async () => {
      if (sinkName) {
        try {
          const response = await ApiService.getSinks();
          const sinkData = Object.values(response.data).find(s => s.name === sinkName);
          if (sinkData) {
            setSink(sinkData);
            setName(sinkData.name);
            setIp(sinkData.ip || '');
            setPort(sinkData.port?.toString() || '4010');
            setBitDepth(sinkData.bit_depth?.toString() || '32');
            setSampleRate(sinkData.sample_rate?.toString() || '48000');
            setChannels(sinkData.channels?.toString() || '2');
            setChannelLayout(sinkData.channel_layout || 'stereo');
            setVolume(sinkData.volume || 1);
            setDelay(sinkData.delay || 0);
            setTimeSync(sinkData.time_sync || false);
            setTimeSyncDelay(sinkData.time_sync_delay?.toString() || '0');
            setProtocol(sinkData.protocol || 'scream');
            setVolumeNormalization(sinkData.volume_normalization || false);
          } else {
            setError(`Sink "${sinkName}" not found.`);
          }
        } catch (error) {
          console.error('Error fetching sink:', error);
          setError('Failed to fetch sink data. Please try again.');
        }
      }
    };

    fetchSink();
  }, [sinkName]);

  /**
   * Handles form submission to add or update a sink.
   * Validates input and sends the data to the API service.
   */
  const handleSubmit = async () => {
    const sinkData: Partial<Sink> = {
      name,
      ip,
      port: parseInt(port),
      bit_depth: parseInt(bitDepth),
      sample_rate: parseInt(sampleRate),
      channels: parseInt(channels),
      channel_layout: channelLayout,
      volume,
      delay,
      time_sync: timeSync,
      time_sync_delay: parseInt(timeSyncDelay),
      protocol: protocol,
      volume_normalization: volumeNormalization,
    };

    try {
      if (isEdit) {
        await ApiService.updateSink(sinkName!, sinkData);
        setSuccess(`Sink "${name}" updated successfully.`);
      } else {
        await ApiService.addSink(sinkData as Sink);
        setSuccess(`Sink "${name}" added successfully.`);
      }
      
      // Clear form if adding a new sink
      if (!isEdit) {
        setName('');
        setIp('');
        setPort('4010');
        setBitDepth('32');
        setSampleRate('48000');
        setChannels('2');
        setChannelLayout('stereo');
        setVolume(1);
        setDelay(0);
        setTimeSync(false);
        setTimeSyncDelay('0');
        setProtocol('scream');
        setVolumeNormalization(false);
      }
    } catch (error) {
      console.error('Error submitting sink:', error);
      setError('Failed to submit sink. Please try again.');
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
          {isEdit ? 'Edit Sink' : 'Add Sink'}
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
            <FormLabel>Sink Name</FormLabel>
            <Input
              value={name}
              onChange={(e) => setName(e.target.value)}
              bg={inputBg}
            />
          </FormControl>
          
          <FormControl isRequired>
            <FormLabel>Sink IP</FormLabel>
            <Input
              value={ip}
              onChange={(e) => setIp(e.target.value)}
              bg={inputBg}
            />
          </FormControl>
          
          <FormControl isRequired>
            <FormLabel>Sink Port</FormLabel>
            <NumberInput
              value={port}
              onChange={(valueString) => setPort(valueString)}
              min={1}
              max={65535}
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
            <FormLabel>Bit Depth</FormLabel>
            <Select
              value={bitDepth}
              onChange={(e) => setBitDepth(e.target.value)}
              bg={inputBg}
            >
              <option value="16">16</option>
              <option value="24">24</option>
              <option value="32">32</option>
            </Select>
          </FormControl>
          
          <FormControl>
            <FormLabel>Sample Rate</FormLabel>
            <Select
              value={sampleRate}
              onChange={(e) => setSampleRate(e.target.value)}
              bg={inputBg}
            >
              <option value="44100">44100</option>
              <option value="48000">48000</option>
              <option value="88200">88200</option>
              <option value="96000">96000</option>
              <option value="192000">192000</option>
            </Select>
          </FormControl>
          
          <FormControl isRequired>
            <FormLabel>Channels</FormLabel>
            <NumberInput
              value={channels}
              onChange={(valueString) => setChannels(valueString)}
              min={1}
              max={8}
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
            <FormLabel>Channel Layout</FormLabel>
            <Select
              value={channelLayout}
              onChange={(e) => setChannelLayout(e.target.value)}
              bg={inputBg}
            >
              <option value="mono">Mono</option>
              <option value="stereo">Stereo</option>
              <option value="quad">Quad</option>
              <option value="surround">Surround</option>
              <option value="5.1">5.1</option>
              <option value="7.1">7.1</option>
            </Select>
          </FormControl>

          <FormControl>
            <FormLabel>Protocol</FormLabel>
            <Select
              value={protocol}
              onChange={(e) => setProtocol(e.target.value)}
              bg={inputBg}
            >
              <option value="scream">Scream</option>
              <option value="rtp">RTP</option>
              <option value="web_receiver">Web Receiver</option>
            </Select>
          </FormControl>
          
          <FormControl>
            <FormLabel>Volume</FormLabel>
            <VolumeSlider value={volume} onChange={setVolume} />
          </FormControl>
          
          <FormControl>
            <FormLabel>Timeshift</FormLabel>
            <TimeshiftSlider value={delay} onChange={setDelay} />
          </FormControl>

          <FormControl>
            <Flex alignItems="center">
              <Checkbox
                isChecked={volumeNormalization}
                onChange={(e) => setVolumeNormalization(e.target.checked)}
                mr={2}
              />
              <FormLabel mb={0}>Volume Normalization</FormLabel>
            </Flex>
          </FormControl>
          
          <FormControl>
            <Flex alignItems="center">
              <Checkbox
                isChecked={timeSync}
                onChange={(e) => setTimeSync(e.target.checked)}
                mr={2}
              />
              <FormLabel mb={0}>Time Sync</FormLabel>
            </Flex>
          </FormControl>
          
          <FormControl>
            <FormLabel>Time Sync Delay (ms)</FormLabel>
            <NumberInput
              value={timeSyncDelay}
              onChange={(valueString) => setTimeSyncDelay(valueString)}
              min={0}
              max={5000}
              bg={inputBg}
              isDisabled={!timeSync}
            >
              <NumberInputField />
              <NumberInputStepper>
                <NumberIncrementStepper />
                <NumberDecrementStepper />
              </NumberInputStepper>
            </NumberInput>
          </FormControl>
        </Stack>
        
        <Flex mt={8} gap={3} justifyContent="flex-end">
          <Button colorScheme="blue" onClick={handleSubmit}>
            {isEdit ? 'Update Sink' : 'Add Sink'}
          </Button>
          <Button variant="outline" onClick={handleClose}>
            Close
          </Button>
        </Flex>
      </Box>
    </Container>
  );
};

export default AddEditSinkPage;