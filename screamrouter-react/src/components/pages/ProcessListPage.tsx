/**
 * React component for displaying a list of processes for a given IP address.
 * This component takes an IP address as a URL parameter and displays a list of processes
 * using the SourceList component.
 */
import React, { useState, useEffect, useMemo } from 'react';
import { useParams } from 'react-router-dom';
import {
  Flex,
  Alert,
  AlertIcon,
  Box,
  Heading,
  useColorModeValue,
  Spinner,
  Text,
  Button
} from '@chakra-ui/react';
import SourceList from '../desktopMenu/list/SourceList';
import ApiService, { Source } from '../../api/api';
import ConfirmationDialog from '../dialogs/ConfirmationDialog';
import { ColorProvider } from '../desktopMenu/context/ColorContext';
import { useAppContext } from '../../context/AppContext';
import { createDesktopMenuActions } from '../desktopMenu/utils';

// Define the type for route parameters
type ProcessListParams = {
  ip: string;
};

const ProcessListPage: React.FC = () => {
  const { ip } = useParams<keyof ProcessListParams>();
  const [processSources, setProcessSources] = useState<Source[]>([]);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);
  const [selectedProcess] = useState<string | null>(null);
  
  // State for delete confirmation dialog
  const [deleteDialogOpen, setDeleteDialogOpen] = useState(false);
  const [deleteItemType, setDeleteItemType] = useState<'sources' | 'sinks' | 'routes' | null>(null);
  const [deleteItemName, setDeleteItemName] = useState<string | null>(null);
  
  // Color values for light/dark mode
  const bgColor = useColorModeValue('white', 'gray.800');
  const borderColor = useColorModeValue('gray.200', 'gray.700');

  // Get context data and functions
  const {
    sources,
    sinks,
    onTranscribeSink,
    onListenToSink,
    onVisualizeSink,
    onToggleActiveSource,
  } = useAppContext();

  // Filter sources for processes with the matching IP
  useEffect(() => {
    if (!ip) {
      setError('IP address is required');
      setLoading(false);
      return;
    }

    console.log("Filtering processes for IP:", ip);
    
    // Filter sources that are processes and have a tag that starts with the IP
    const processesForIp = sources.filter(source => 
      source.is_process && source.tag && source.tag.startsWith(ip)
    );
    
    setProcessSources(processesForIp);
    console.log(`Found ${processesForIp.length} processes for IP ${ip}`);
    setLoading(false);
  }, [ip, sources]); // Re-run when sources or IP changes

  // Function to navigate to a specific item
  const navigateToItem = (type: 'sources' | 'sinks' | 'routes', itemName: string) => {
    // In this context, we don't need to navigate to another menu
    console.log(`Navigate to ${type} ${itemName}`);
  };
  
  // Function to set starred items
  const setStarredItemsHandler = (type: 'sources' | 'sinks' | 'routes', _setter: (prev: string[]) => string[]) => {
    // We don't need to handle starring in this context
    console.log(`Set starred ${type}`);
  };
  
  // Function to handle delete confirmation
  const handleConfirmDelete = async () => {
    if (!deleteItemType || !deleteItemName) return;
    
    try {
      if (deleteItemType === 'sources') {
        await ApiService.deleteSource(deleteItemName);
      } else if (deleteItemType === 'sinks') {
        await ApiService.deleteSink(deleteItemName);
      } else if (deleteItemType === 'routes') {
        await ApiService.deleteRoute(deleteItemName);
      }
    } catch (error) {
      console.error(`Error deleting ${deleteItemType}:`, error);
      setError(`Error deleting ${deleteItemType}`);
    } finally {
      // Reset dialog state
      setDeleteDialogOpen(false);
      setDeleteItemType(null);
      setDeleteItemName(null);
    }
  };
  
  // Function to open delete confirmation dialog
  const openDeleteDialog = (type: 'sources' | 'sinks' | 'routes', name: string) => {
    setDeleteItemType(type);
    setDeleteItemName(name);
    setDeleteDialogOpen(true);
  };

  // Create real actions for the SourceList component
  const actions = useMemo(() => {
    const baseActions = createDesktopMenuActions(
      setStarredItemsHandler,
      setError,
      onToggleActiveSource,
      onTranscribeSink,
      (name: string | null) => onListenToSink(name ? sinks.find(s => s.name === name) || null : null),
      (name: string | null) => onVisualizeSink(name ? sinks.find(s => s.name === name) || null : null),
      navigateToItem
    );
    
    // Override the confirmDelete action with our local implementation
    baseActions.confirmDelete = openDeleteDialog;
    
    return baseActions;
  }, [onToggleActiveSource, onTranscribeSink, onListenToSink, onVisualizeSink, sinks]);

  const handleClose = () => {
    window.close();
  };

  return (
    <Box width="100%" height="100vh" p={4}>
      {/* Delete Confirmation Dialog */}
      <ConfirmationDialog
        isOpen={deleteDialogOpen}
        onClose={() => setDeleteDialogOpen(false)}
        onConfirm={handleConfirmDelete}
        title="Delete Item"
        message={`Are you sure you want to delete ${deleteItemName}? This action cannot be undone.`}
      />
      <Box
        bg={bgColor}
        borderColor={borderColor}
        borderWidth="1px"
        borderRadius="lg"
        p={6}
        boxShadow="md"
        height="100%"
        display="flex"
        flexDirection="column"
      >
        <Heading as="h1" size="lg" mb={6}>
          Processes for IP: {ip}
        </Heading>
        
        {error && (
          <Alert status="error" mb={4} borderRadius="md">
            <AlertIcon />
            {error}
          </Alert>
        )}
        
        {loading ? (
          <Flex justify="center" align="center" py={10} flex="1">
            <Spinner size="xl" />
          </Flex>
        ) : processSources.length > 0 ? (
          <Box overflowX="auto" flex="1">
            <ColorProvider>
              <SourceList
                sources={processSources}
                routes={[]}
                starredSources={[]}
                activeSource={null}
                actions={actions}
                selectedItem={selectedProcess}
                showProcesses={true}
              />
            </ColorProvider>
          </Box>
        ) : (
          <Flex justify="center" align="center" py={10} flex="1">
            <Text color="gray.500">
              No processes found for IP: {ip}
            </Text>
          </Flex>
        )}
        
        <Flex mt={4} justifyContent="flex-end">
          <Button variant="outline" onClick={handleClose}>
            Close
          </Button>
        </Flex>
      </Box>
    </Box>
  );
};

export default ProcessListPage;
