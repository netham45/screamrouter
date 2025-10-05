import React, { useEffect, useState, useRef } from 'react';
import { useParams } from 'react-router-dom';
import { useAppContext } from '../../context/AppContext';
import { Box, Heading, Text, Button, Center, VStack, Spinner } from '@chakra-ui/react';

const ListenPage: React.FC = () => {
  const { sinkName } = useParams<{ sinkName: string }>();
  const { onListenToSink, listeningStatus, playbackError, silenceSessionAudioRef, isSilenceAudioPlaying, startedListeningSinks, setStartedListeningSinks } = useAppContext();

  const isListening = sinkName ? listeningStatus.get(sinkName) || false : false;
  const startedListening = sinkName ? startedListeningSinks.get(sinkName) || false : false;
  const [connectionStatus, setConnectionStatus] = useState<'disconnected' | 'connecting' | 'connected' | 'reconnecting'>('disconnected');
  const retryTimeoutRef = useRef<ReturnType<typeof setTimeout> | null>(null);
  const retryCountRef = useRef(0);

  // Use a ref to hold the latest onListenToSink function without causing the effect to re-run.
  const onListenToSinkRef = useRef(onListenToSink);
  useEffect(() => {
    onListenToSinkRef.current = onListenToSink;
  });

  useEffect(() => {
    if (sinkName) {
      document.title = isListening ? `Listening to ${sinkName}` : `Not listening to ${sinkName}`;
      if (isListening) {
        setConnectionStatus('connected');
        retryCountRef.current = 0; // Reset retry count on successful connection
      }
    }
  }, [isListening, sinkName]);

  useEffect(() => {
    // If there's a playback error for this sink, stop trying to listen.
    if (sinkName && playbackError.has(sinkName)) {
      setStartedListeningSinks(prev => new Map(prev).set(sinkName, false));
    }
  }, [playbackError, sinkName]);

  useEffect(() => {
    if (!isSilenceAudioPlaying) {
      if (sinkName) {
        setStartedListeningSinks(prev => new Map(prev).set(sinkName, false));
      }
    }
  }, [isSilenceAudioPlaying, sinkName, setStartedListeningSinks]);

  useEffect(() => {
    const handleVisibilityChange = () => {
      if (document.visibilityState === 'visible' && startedListening && !isListening && sinkName && isSilenceAudioPlaying) {
        console.log("Page is visible again, trying to reconnect.");
        onListenToSinkRef.current(sinkName);
      }
    };

    document.addEventListener('visibilitychange', handleVisibilityChange);

    return () => {
      document.removeEventListener('visibilitychange', handleVisibilityChange);
    };
  }, [startedListening, isListening, sinkName, isSilenceAudioPlaying]);

  useEffect(() => {
    // On initial load, try to start listening.
    if (sinkName) {
      setStartedListeningSinks(prev => new Map(prev).set(sinkName, true));
    }
  }, [sinkName]);

  useEffect(() => {
    // This effect handles the retry logic.
    if (startedListening && !isListening && sinkName) {
      setConnectionStatus(retryCountRef.current > 0 ? 'reconnecting' : 'connecting');
      
      // Clear any existing retry timeout
      if (retryTimeoutRef.current) {
        clearTimeout(retryTimeoutRef.current);
      }

      const retryDelay = Math.min(1000 * (2 ** retryCountRef.current), 30000); // Exponential backoff
      console.log(`Attempting to connect in ${retryDelay}ms`);

      retryTimeoutRef.current = setTimeout(() => {
        onListenToSinkRef.current(sinkName);
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
  }, [startedListening, isListening, sinkName]);

  const handleToggleListen = () => {
    if (isListening) {
      // If we are listening, we want to stop.
      if (sinkName) {
        setStartedListeningSinks(prev => new Map(prev).set(sinkName, false));
      }
      silenceSessionAudioRef.current?.pause();
      if (sinkName) {
        onListenToSink(sinkName); // Stop immediately.
      }
      retryCountRef.current = 0;
      if (retryTimeoutRef.current) {
        clearTimeout(retryTimeoutRef.current);
      }
    } else {
      // If we are not listening, we want to start.
      silenceSessionAudioRef.current?.play();
      if (sinkName) {
        setStartedListeningSinks(prev => new Map(prev).set(sinkName, true));
      }
      if (sinkName) {
        onListenToSink(sinkName); // Try to connect immediately.
      }
    }
  };

  if (!sinkName) {
    return (
      <Center height="100vh" bg="gray.800" color="white">
        <Heading>No sink specified.</Heading>
      </Center>
    );
  }

  return (
    <Center height="100vh" bg="gray.800" color="white">
      <VStack spacing={8}>
        <Box textAlign="center">
          <Heading as="h1" size="2xl" mb={4}>
            ScreamRouter Listener
          </Heading>
          {startedListening ? (
            <VStack>
              <Text fontSize="xl">{connectionStatus === 'connected' ? "Listening to" : "Attempting to listen to"}</Text>
              <Text fontSize="3xl" fontWeight="bold">{sinkName}</Text>
              {connectionStatus !== 'disconnected' && <Spinner size="xl" color="green.300" mt={4} />}
            </VStack>
          ) : (
            <VStack>
              <Text fontSize="xl">Not listening to</Text>
              <Text fontSize="3xl" fontWeight="bold">{sinkName}</Text>
            </VStack>
          )}
        </Box>
        <Button 
          colorScheme={isListening ? "red" : "green"} 
          onClick={handleToggleListen}
          size="lg"
          px={10}
          py={6}
        >
          {startedListening ? 'Stop Listening' : `Listen to ${sinkName}`}
        </Button>
        <Text>
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
