/**
 * React component for adding or editing a group (source or sink) in a standalone page.
 * This component provides a form to input details about a group, including its name, type (source or sink),
 * and members.
 * It allows the user to either add a new group or update an existing one.
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
  Alert,
  AlertIcon,
  Box,
  Heading,
  Container,
  useColorModeValue,
  Switch,
  CheckboxGroup,
  Checkbox,
  VStack
} from '@chakra-ui/react';
import ApiService, { Source, Sink } from '../../api/api';

const AddEditGroupPage: React.FC = () => {
  const [searchParams] = useSearchParams();
  const groupName = searchParams.get('name');
  const groupType = searchParams.get('type') as 'source' | 'sink' || 'source';
  const isEdit = !!groupName;

  // eslint-disable-next-line @typescript-eslint/no-unused-vars
  const [group, setGroup] = useState<Source | Sink | null>(null);
  const [name, setName] = useState('');
  const [enabled, setEnabled] = useState(true);
  const [members, setMembers] = useState<string[]>([]);
  
  const [availableMembers, setAvailableMembers] = useState<(Source | Sink)[]>([]);
  const [error, setError] = useState<string | null>(null);
  const [success, setSuccess] = useState<string | null>(null);

  // Color values for light/dark mode
  const bgColor = useColorModeValue('white', 'gray.800');
  const borderColor = useColorModeValue('gray.200', 'gray.700');
  const inputBg = useColorModeValue('white', 'gray.700');

  // Fetch available members based on group type
  useEffect(() => {
    const fetchMembers = async () => {
      try {
        if (groupType === 'source') {
          const response = await ApiService.getSources();
          const sources = Object.values(response.data).filter(s => !s.is_group);
          setAvailableMembers(sources);
        } else {
          const response = await ApiService.getSinks();
          const sinks = Object.values(response.data).filter(s => !s.is_group);
          setAvailableMembers(sinks);
        }
      } catch (error) {
        console.error(`Error fetching ${groupType}s:`, error);
        setError(`Failed to fetch ${groupType}s. Please try again.`);
      }
    };

    fetchMembers();
  }, [groupType]);

  // Fetch group data if editing
  useEffect(() => {
    const fetchGroup = async () => {
      if (groupName) {
        try {
          if (groupType === 'source') {
            const response = await ApiService.getSources();
            const sourceData = Object.values(response.data).find(s => s.name === groupName);
            if (sourceData) {
              setGroup(sourceData);
              setName(sourceData.name);
              setEnabled(sourceData.enabled);
              setMembers(sourceData.group_members || []);
            } else {
              setError(`Source group "${groupName}" not found.`);
            }
          } else {
            const response = await ApiService.getSinks();
            const sinkData = Object.values(response.data).find(s => s.name === groupName);
            if (sinkData) {
              setGroup(sinkData);
              setName(sinkData.name);
              setEnabled(sinkData.enabled);
              setMembers(sinkData.group_members || []);
            } else {
              setError(`Sink group "${groupName}" not found.`);
            }
          }
        } catch (error) {
          console.error(`Error fetching ${groupType} group:`, error);
          setError(`Failed to fetch ${groupType} group data. Please try again.`);
        }
      }
    };

    fetchGroup();
  }, [groupName, groupType]);

  /**
   * Handles form submission to add or update a group.
   * Validates input and sends the data to the API service.
   */
  const handleSubmit = async () => {
    if (members.length === 0) {
      setError('Please select at least one member for the group.');
      return;
    }

    const groupData = {
      name,
      enabled,
      is_group: true,
      group_members: members
    };

    try {
      if (isEdit) {
        if (groupType === 'source') {
          await ApiService.updateSource(groupName!, groupData);
        } else {
          await ApiService.updateSink(groupName!, groupData);
        }
        setSuccess(`${groupType.charAt(0).toUpperCase() + groupType.slice(1)} group "${name}" updated successfully.`);
      } else {
        if (groupType === 'source') {
          await ApiService.addSource(groupData as Source);
        } else {
          await ApiService.addSink(groupData as Sink);
        }
        setSuccess(`${groupType.charAt(0).toUpperCase() + groupType.slice(1)} group "${name}" added successfully.`);
      }
      
      // Clear form if adding a new group
      if (!isEdit) {
        setName('');
        setEnabled(true);
        setMembers([]);
      }
    } catch (error) {
      console.error(`Error submitting ${groupType} group:`, error);
      setError(`Failed to submit ${groupType} group. Please try again.`);
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
          {isEdit ? `Edit ${groupType.charAt(0).toUpperCase() + groupType.slice(1)} Group` : `Add ${groupType.charAt(0).toUpperCase() + groupType.slice(1)} Group`}
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
            <FormLabel>Group Name</FormLabel>
            <Input
              value={name}
              onChange={(e) => setName(e.target.value)}
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
          
          <FormControl isRequired>
            <FormLabel>Group Members</FormLabel>
            <Box 
              borderWidth="1px" 
              borderRadius="md" 
              p={4} 
              bg={inputBg}
              maxH="300px"
              overflowY="auto"
            >
              <CheckboxGroup 
                colorScheme="blue" 
                value={members}
                onChange={(values) => setMembers(values as string[])}
              >
                <VStack align="start" spacing={2}>
                  {availableMembers.map(member => (
                    <Checkbox key={member.name} value={member.name}>
                      {member.name}
                    </Checkbox>
                  ))}
                </VStack>
              </CheckboxGroup>
            </Box>
          </FormControl>
        </Stack>
        
        <Flex mt={8} gap={3} justifyContent="flex-end">
          <Button colorScheme="blue" onClick={handleSubmit}>
            {isEdit ? 'Update Group' : 'Add Group'}
          </Button>
          <Button variant="outline" onClick={handleClose}>
            Close
          </Button>
        </Flex>
      </Box>
    </Container>
  );
};

export default AddEditGroupPage;