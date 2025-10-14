import React, { useEffect, useState, useRef } from 'react';
import { useParams } from 'react-router-dom';
import { useAppContext } from '../../context/AppContext';
import { Box, Heading, Text, Button, Center, VStack, Spinner, Icon, Badge } from '@chakra-ui/react';
import { FaVolumeUp, FaMicrophone, FaRoute } from 'react-icons/fa';

type EntityType = 'sink' | 'source' | 'route';

const ListenPage: React.FC = () => {
  const { entityType, entityName } = useParams<{ entityType: string; entityName: string }>();
  const { onListenToEntity, listeningStatus, playbackError, silenceSessionAudioRef, isSilenceAudioPlaying, startedListeningSinks, setStartedListeningSinks } = useAppContext();

  // Validate and cast entity type
  const validEntityType = (entityType as EntityType) || 'sink';
  const isValidType = ['sink', 'source', 'route'].includes(validEntityType);

  const isListening = entityName ? listeningStatus.get(entityName) || false : false;
  const startedListening = entityName ? startedListeningSinks.get(entityName) || false : false;
  const [connectionStatus, setConnectionStatus] = useState<'disconnected' | 'connecting' | 'connected' | 'reconnecting'>('disconnected');
  const retryTimeoutRef = useRef<ReturnType<typeof setTimeout> | null>(null);
  const retryCountRef = useRef(0);

  // Use a ref to hold the latest onListenToEntity function without causing the effect to re-run.
  const onListenToEntityRef = useRef(onListenToEntity);
  useEffect(() => {
    onListenToEntityRef.current = onListenToEntity;
  });

  useEffect(() => {
    if (entityName && isValidType) {
      const entityLabel = `${validEntityType} ${entityName}`;
      document.title = isListening ? `Listening to ${entityLabel}` : `Not listening to ${entityLabel}`;
      if (isListening) {
        setConnectionStatus('connected');
        retryCountRef.current = 0; // Reset retry count on successful connection
      }
    }
  }, [isListening, entityName, validEntityType, isValidType]);

  useEffect(() => {
    // If there's a playback error for this entity, stop trying to listen.
    if (entityName && playbackError.has(entityName)) {
      setStartedListeningSinks(prev => new Map(prev).set(entityName, false));
    }
  }, [playbackError, entityName, setStartedListeningSinks]);

  useEffect(() => {
    if (!isSilenceAudioPlaying) {
      if (entityName) {
        setStartedListeningSinks(prev => new Map(prev).set(entityName, false));
      }
    }
  }, [isSilenceAudioPlaying, entityName, setStartedListeningSinks]);

  useEffect(() => {
    const handleVisibilityChange = () => {
      if (document.visibilityState === 'visible' && startedListening && !isListening && entityName && isSilenceAudioPlaying && isValidType) {
        console.log("Page is visible again, trying to reconnect.");
        onListenToEntityRef.current(validEntityType, entityName);
      }
    };

    document.addEventListener('visibilitychange', handleVisibilityChange);

    return () => {
      document.removeEventListener('visibilitychange', handleVisibilityChange);
    };
  }, [startedListening, isListening, entityName, isSilenceAudioPlaying, isValidType, validEntityType]);

  useEffect(() => {
    // On initial load, try to start listening.
    if (entityName && isValidType) {
      setStartedListeningSinks(prev => new Map(prev).set(entityName, true));
    }
  }, [entityName, isValidType, setStartedListeningSinks]);

  useEffect(() => {
    // This effect handles the retry logic.
    if (startedListening && !isListening && entityName && isValidType) {
      setConnectionStatus(retryCountRef.current > 0 ? 'reconnecting' : 'connecting');
      
      // Clear any existing retry timeout
      if (retryTimeoutRef.current) {
        clearTimeout(retryTimeoutRef.current);
      }

      const retryDelay = Math.min(1000 * (2 ** retryCountRef.current), 30000); // Exponential backoff
      console.log(`Attempting to connect to ${validEntityType} in ${retryDelay}ms`);

      retryTimeoutRef.current = setTimeout(() => {
        onListenToEntityRef.current(validEntityType, entityName);
        retryCountRef.current++;
      }, retryDelay);

      return () => {
        if (retryTimeoutRef.current) {
          clearTimeout(retryTimeoutRef.current);
        }
      };
    } else if (!startedListening) {
        setConnectionStatus('disconnected');
        if (retryTimeoutRef.current) {
            clearTimeout(retryTimeoutRef.current);
        }
    }
  }, [startedListening, isListening, entityName, validEntityType, isValidType]);

  const handleToggleListen = () => {
    if (!isValidType || !entityName) return;

    if (isListening) {
      // If we are listening, we want to stop.
      setStartedListeningSinks(prev => new Map(prev).set(entityName, false));
      silenceSessionAudioRef.current?.pause();
      onListenToEntity(validEntityType, entityName); // Stop immediately.
      retryCountRef.current = 0;
      if (retryTimeoutRef.current) {
        clearTimeout(retryTimeoutRef.current);
      }
    } else {
      // If we are not listening, we want to start.
      silenceSessionAudioRef.current?.play();
      setStartedListeningSinks(prev => new Map(prev).set(entityName, true));
      onListenToEntity(validEntityType, entityName); // Try to connect immediately.
    }
  };

  // Get the appropriate icon and color based on entity type
  const getEntityIcon = () => {
    switch (validEntityType) {
      case 'source':
        return FaMicrophone;
      case 'route':
        return FaRoute;
      case 'sink':
      default:
        return FaVolumeUp;
    }
  };

  const getEntityColor = () => {
    switch (validEntityType) {
      case 'source':
        return 'blue';
      case 'route':
        return 'purple';
      case 'sink':
      default:
        return 'green';
    }
  };

  const getEntityBgColor = () => {
    switch (validEntityType) {
      case 'source':
        return 'blue.900';
      case 'route':
        return 'purple.900';
      case 'sink':
      default:
        return 'gray.800';
    }
  };

  if (!entityName || !isValidType) {
    return (
      <Center height="100vh" bg="gray.800" color="white">
        <VStack spacing={4}>
          <Heading>Invalid entity specified.</Heading>
          {!isValidType && <Text>Entity type must be one of: sink, source, or route</Text>}
          {!entityName && <Text>No entity name provided</Text>}
        </VStack>
      </Center>
    );
  }

  const EntityIcon = getEntityIcon();
  const entityColor = getEntityColor();
  const bgColor = getEntityBgColor();

  return (
    <Center height="100vh" bg={bgColor} color="white">
      <VStack spacing={8}>
        <Box textAlign="center">
          <Heading as="h1" size="2xl" mb={4}>
            ScreamRouter Listener
          </Heading>
          
          {/* Entity type badge with icon */}
          <Badge
            colorScheme={entityColor}
            fontSize="lg"
            px={4}
            py={2}
            mb={4}
            display="inline-flex"
            alignItems="center"
            gap={2}
          >
            <Icon as={EntityIcon} />
            {validEntityType.toUpperCase()}
          </Badge>

          {startedListening ? (
            <VStack>
              <Text fontSize="xl">{connectionStatus === 'connected' ? "Listening to" : "Attempting to listen to"}</Text>
              <Box display="flex" alignItems="center" gap={3}>
                <Icon as={EntityIcon} boxSize={8} color={`${entityColor}.300`} />
                <Text fontSize="3xl" fontWeight="bold">{entityName}</Text>
              </Box>
              {connectionStatus !== 'disconnected' && (
                <Spinner size="xl" color={`${entityColor}.300`} mt={4} />
              )}
            </VStack>
          ) : (
            <VStack>
              <Text fontSize="xl">Not listening to</Text>
              <Box display="flex" alignItems="center" gap={3}>
                <Icon as={EntityIcon} boxSize={8} color={`${entityColor}.300`} />
                <Text fontSize="3xl" fontWeight="bold">{entityName}</Text>
              </Box>
            </VStack>
          )}
        </Box>
        
        <Button
          colorScheme={isListening ? "red" : entityColor}
          onClick={handleToggleListen}
          size="lg"
          px={10}
          py={6}
          leftIcon={<Icon as={EntityIcon} />}
        >
          {startedListening ? 'Stop Listening' : `Listen to ${entityName}`}
        </Button>
        
        <Text color={`${entityColor}.200`}>
          {connectionStatus === 'connecting' && "Connecting..."}
          {connectionStatus === 'connected' && "Connected"}
          {connectionStatus === 'reconnecting' && "Reconnecting..."}
          {connectionStatus === 'disconnected' && !startedListening && "Ready to listen"}
          {connectionStatus === 'disconnected' && startedListening && "Disconnected"}
        </Text>
      </VStack>
    </Center>
  );
};

export default ListenPage;
