/**
 * React component for adding or editing a source in a standalone page.
 * This component provides a form to input details about a source, including its name, IP address,
 * volume, delay, timeshift, and VNC settings.
 * It allows the user to either add a new source or update an existing one.
 */
import React, { useState, useEffect } from 'react';
import { useSearchParams } from 'react-router-dom';
import {
  Flex,
  FormControl,
  FormLabel,
  Input,
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
import ApiService, { Source } from '../../api/api';
import VolumeSlider from './controls/VolumeSlider';
import TimeshiftSlider from './controls/TimeshiftSlider';

const AddEditSourcePage: React.FC = () => {
  const [searchParams] = useSearchParams();
  const sourceName = searchParams.get('name');
  const isEdit = !!sourceName;

  // eslint-disable-next-line @typescript-eslint/no-unused-vars
  const [source, setSource] = useState<Source | null>(null);
  const [name, setName] = useState('');
  const [ip, setIp] = useState('');
  const [enabled, setEnabled] = useState(true);
  const [isGroup, setIsGroup] = useState(false);
  const [groupMembers, setGroupMembers] = useState<string[]>([]);
  const [volume, setVolume] = useState(1);
  const [delay, setDelay] = useState(0);
  const [timeshift, setTimeshift] = useState(0);
  const [vncIp, setVncIp] = useState('');
  const [vncPort, setVncPort] = useState('5900');
  const [error, setError] = useState<string | null>(null);
  const [success, setSuccess] = useState<string | null>(null);

  // Color values for light/dark mode
  const bgColor = useColorModeValue('white', 'gray.800');
  const borderColor = useColorModeValue('gray.200', 'gray.700');
  const inputBg = useColorModeValue('white', 'gray.700');

  // Fetch source data if editing
  useEffect(() => {
    const fetchSource = async () => {
      if (sourceName) {
        try {
          const response = await ApiService.getSources();
          const sourceData = Object.values(response.data).find(s => s.name === sourceName);
          if (sourceData) {
            setSource(sourceData);
            setName(sourceData.name);
            setIp(sourceData.ip || '');
            setEnabled(sourceData.enabled);
            setIsGroup(sourceData.is_group);
            setGroupMembers(sourceData.group_members || []);
            setVolume(sourceData.volume || 1);
            setDelay(sourceData.delay || 0);
            setTimeshift(sourceData.timeshift || 0);
            setVncIp(sourceData.vnc_ip || '');
            setVncPort(sourceData.vnc_port?.toString() || '5900');
          } else {
            setError(`Source "${sourceName}" not found.`);
          }
        } catch (error) {
          console.error('Error fetching source:', error);
          setError('Failed to fetch source data. Please try again.');
        }
      }
    };

    fetchSource();
  }, [sourceName]);

  /**
   * Handles form submission to add or update a source.
   * Validates input and sends the data to the API service.
   */
  const handleSubmit = async () => {
    const sourceData: Partial<Source> = {
      name,
      ip,
      enabled,
      is_group: isGroup,
      group_members: groupMembers,
      volume,
      delay,
      timeshift,
      vnc_ip: vncIp || undefined,
      vnc_port: vncIp ? parseInt(vncPort) : undefined
    };

    try {
      if (isEdit) {
        await ApiService.updateSource(sourceName!, sourceData);
        setSuccess(`Source "${name}" updated successfully.`);
      } else {
        await ApiService.addSource(sourceData as Source);
        setSuccess(`Source "${name}" added successfully.`);
      }
      
      // Clear form if adding a new source
      if (!isEdit) {
        setName('');
        setIp('');
        setEnabled(true);
        setIsGroup(false);
        setGroupMembers([]);
        setVolume(1);
        setDelay(0);
        setTimeshift(0);
        setVncIp('');
        setVncPort('5900');
      }
    } catch (error) {
      console.error('Error submitting source:', error);
      setError('Failed to submit source. Please try again.');
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
          {isEdit ? 'Edit Source' : 'Add Source'}
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
            <FormLabel>Source Name</FormLabel>
            <Input
              value={name}
              onChange={(e) => setName(e.target.value)}
              bg={inputBg}
            />
          </FormControl>
          
          <FormControl isRequired>
            <FormLabel>Source IP</FormLabel>
            <Input
              value={ip}
              onChange={(e) => setIp(e.target.value)}
              bg={inputBg}
            />
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
              onChange={(valueString) => setDelay(parseInt(valueString))}
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
          
          <Heading as="h3" size="md" mt={4} mb={2}>
            VNC Settings (Optional)
          </Heading>
          
          <FormControl>
            <FormLabel>VNC IP</FormLabel>
            <Input
              value={vncIp}
              onChange={(e) => setVncIp(e.target.value)}
              bg={inputBg}
              placeholder="Leave empty if not using VNC"
            />
          </FormControl>
          
          <FormControl>
            <FormLabel>VNC Port</FormLabel>
            <NumberInput
              value={vncPort}
              onChange={(valueString) => setVncPort(valueString)}
              min={1}
              max={65535}
              bg={inputBg}
              isDisabled={!vncIp}
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
            {isEdit ? 'Update Source' : 'Add Source'}
          </Button>
          <Button variant="outline" onClick={handleClose}>
            Close
          </Button>
        </Flex>
      </Box>
    </Container>
  );
};

export default AddEditSourcePage;