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
  Switch,
  Textarea
} from '@chakra-ui/react';
import ApiService, { Source } from '../../api/api';
import { useTutorial } from '../../context/TutorialContext';
import { useMdnsDiscovery } from '../../context/MdnsDiscoveryContext';
import VolumeSlider from './controls/VolumeSlider';
import TimeshiftSlider from './controls/TimeshiftSlider';

const AddEditSourcePage: React.FC = () => {
  const [searchParams] = useSearchParams();
  const sourceName = searchParams.get('name');
  const isEdit = !!sourceName;

  const { completeStep, nextStep } = useTutorial();
  const { openModal: openMdnsModal, registerSelectionHandler } = useMdnsDiscovery();

  // eslint-disable-next-line @typescript-eslint/no-unused-vars
  const [source, setSource] = useState<Source | null>(null);
  const [name, setName] = useState('');
  const [ip, setIp] = useState('');
  const [enabled, setEnabled] = useState(true);
  const [isGroup, setIsGroup] = useState(false);
  const [groupMembers, setGroupMembers] = useState<string[]>([]);
  const [groupMembersText, setGroupMembersText] = useState('');
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

  useEffect(() => {
    if (!name.trim()) {
      return;
    }
    completeStep('source-name-input');
  }, [name, completeStep]);

  useEffect(() => {
    if (!ip.trim()) {
      return;
    }
    completeStep('source-ip-input');
  }, [ip, completeStep]);

  useEffect(() => {
    if (typeof window === 'undefined') {
      return;
    }
    const handleBeforeUnload = () => {
      try {
        const targetOrigin = window.location.origin;
        window.opener?.postMessage(
          {
            type: 'FORM_WINDOW_CLOSING',
            form: 'source',
          },
          targetOrigin
        );
      } catch (error) {
        console.error('Failed to announce form window closing', error);
      }
    };

    window.addEventListener('beforeunload', handleBeforeUnload);
    return () => {
      window.removeEventListener('beforeunload', handleBeforeUnload);
    };
  }, []);

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
            setGroupMembersText((sourceData.group_members || []).join(', '));
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

  useEffect(() => {
    const unregister = registerSelectionHandler(device => {
      if (device.name) {
        setName(prev => prev || device.name);
      }
      if (device.ip) {
        setIp(device.ip);
      }
    });

    return unregister;
  }, [registerSelectionHandler]);

  const handleGroupMembersChange = (value: string) => {
    setGroupMembersText(value);
    const members = value
      .split(',')
      .map(member => member.trim())
      .filter(member => member.length > 0);
    setGroupMembers(members);
  };

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

      completeStep('source-submit');
      nextStep();

      try {
        const targetOrigin = window.location.origin;
        window.opener?.postMessage(
          {
            type: 'RESOURCE_ADDED',
            resourceType: 'source',
            action: isEdit ? 'updated' : 'added',
            name,
          },
          targetOrigin
        );
      } catch (messageError) {
        console.error('Failed to post resource update message', messageError);
      }

      // Clear form if adding a new source
      if (!isEdit) {
        setName('');
        setIp('');
        setEnabled(true);
        setIsGroup(false);
        setGroupMembers([]);
        setGroupMembersText('');
        setVolume(1);
        setDelay(0);
        setTimeshift(0);
        setVncIp('');
        setVncPort('5900');
      }
      if (window.opener) {
        setTimeout(() => {
          try {
            window.close();
          } catch (closeError) {
            console.error('Failed to close window after source submission', closeError);
          }
        }, 200);
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
              data-tutorial-id="source-name-input"
              value={name}
              onChange={(e) => setName(e.target.value)}
              bg={inputBg}
            />
          </FormControl>
          
          <FormControl isRequired>
            <FormLabel>Source IP</FormLabel>
            <Stack
              direction={{ base: 'column', md: 'row' }}
              spacing={2}
              align={{ base: 'stretch', md: 'center' }}
            >
              <Input
                data-tutorial-id="source-ip-input"
                value={ip}
                onChange={(e) => setIp(e.target.value)}
                bg={inputBg}
                flex="1"
              />
              <Button
                onClick={() => openMdnsModal('sources')}
                variant="outline"
                colorScheme="blue"
                width={{ base: '100%', md: 'auto' }}
              >
                Discover Devices
              </Button>
            </Stack>
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
            <VolumeSlider
              value={volume}
              onChange={setVolume}
              dataTutorialId="source-volume-slider"
            />
          </FormControl>

          <FormControl>
            <FormLabel>Delay (ms)</FormLabel>
            <NumberInput
              data-tutorial-id="source-delay-input"
              value={delay}
              onChange={(valueString) => {
                const parsed = Number.parseInt(valueString, 10);
                setDelay(Number.isNaN(parsed) ? 0 : parsed);
              }}
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
            <TimeshiftSlider
              value={timeshift}
              onChange={setTimeshift}
              dataTutorialId="source-timeshift-slider"
            />
          </FormControl>

          <Heading as="h3" size="md" mt={4} mb={2}>
            VNC Settings (Optional)
          </Heading>
          
          <FormControl>
            <FormLabel>VNC IP</FormLabel>
            <Input
              data-tutorial-id="source-vnc-ip-input"
              value={vncIp}
              onChange={(e) => setVncIp(e.target.value)}
              bg={inputBg}
              placeholder="Leave empty if not using VNC"
            />
          </FormControl>

          <FormControl>
            <FormLabel>VNC Port</FormLabel>
            <NumberInput
              data-tutorial-id="source-vnc-port-input"
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
          <Button
            colorScheme="blue"
            onClick={handleSubmit}
            data-tutorial-id="source-submit-button"
          >
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
