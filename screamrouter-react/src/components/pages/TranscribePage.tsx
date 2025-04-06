/**
 * React component for displaying transcription data from a WebSocket connection.
 * It connects to a WebSocket endpoint and displays the most recent message received.
 * Uses Chakra UI components for consistent styling.
 */
import React, { useEffect, useState } from 'react';
import { Box, Text, Heading, useColorModeValue } from '@chakra-ui/react';
import { useSearchParams, useParams } from 'react-router-dom';

/**
 * Interface defining the props for the TranscribePage component.
 */
interface TranscribePageProps {
  ip?: string;
}

/**
 * React functional component for rendering the transcription page.
 *
 * @param {TranscribePageProps} props - The props passed to the component.
 * @returns {JSX.Element} The rendered JSX element.
 */
const TranscribePage: React.FC<TranscribePageProps> = ({ ip: propIp }) => {
  // Get IP from URL params or route params if not provided in props
  const [searchParams] = useSearchParams();
  const params = useParams();
  const urlIp = searchParams.get('ip');
  const routeIp = params.ip;
  const ip = propIp || routeIp || urlIp;

  // State for the most recent message
  const [message, setMessage] = useState<string>('Connecting to transcription service...');
  // State to track connection status
  const [isConnected, setIsConnected] = useState<boolean>(false);
  // State to track reconnection attempts
  const [reconnectAttempt, setReconnectAttempt] = useState<number>(0);

  // Colors for light/dark mode
  const textColor = useColorModeValue('gray.800', 'gray.100');
  const borderColor = useColorModeValue('gray.200', 'gray.700');

  // Function to create and setup WebSocket connection
  const setupWebSocket = () => {
    // Return early if no IP is provided
    if (!ip) {
      setMessage('Error: No IP address provided');
      return null;
    }

    // Create WebSocket connection
    const wsUrl = `ws://127.0.0.1:8085/transcribe/${ip}/`;
    const ws = new WebSocket(wsUrl);

    // WebSocket event handlers
    ws.onopen = () => {
      console.log('WebSocket connection established');
      setMessage('Connected. Waiting for transcription data...');
      setIsConnected(true);
      setReconnectAttempt(0);
    };

    ws.onmessage = (event) => {
      console.log('WebSocket message received:', event.data);
      // Replace newlines with <br/> tags
      const formattedMessage = event.data.replace(/\n/g, '<br/>');
      setMessage(formattedMessage);
    };

    ws.onerror = (error) => {
      console.error('WebSocket error:', error);
      setMessage(`Error connecting to transcription service. Attempting to reconnect...`);
      setIsConnected(false);
    };

    ws.onclose = () => {
      console.log('WebSocket connection closed');
      setMessage('Connection closed. Attempting to reconnect...');
      setIsConnected(false);
    };

    return ws;
  };

  useEffect(() => {
    // Set document title
    document.title = `ScreamRouter - Transcribe`;

    const ws = setupWebSocket();

    // Clean up WebSocket connection on component unmount
    return () => {
      if (ws && ws.readyState === WebSocket.OPEN) {
        ws.close();
      }
    };
  }, [ip]);

  // Effect for handling reconnection
  useEffect(() => {
    let reconnectTimer: NodeJS.Timeout | null = null;

    // Only attempt to reconnect if not connected and IP is provided
    if (!isConnected && ip) {
      reconnectTimer = setTimeout(() => {
        console.log(`Attempting to reconnect (attempt ${reconnectAttempt + 1})...`);
        setReconnectAttempt(prev => prev + 1);
        
        // Create a new WebSocket connection
        const ws = setupWebSocket();
        
        // Clean up this WebSocket on next reconnect attempt
        return () => {
          if (ws && ws.readyState !== WebSocket.CLOSED) {
            ws.close();
          }
        };
      }, 3000); // Reconnect every 3 seconds
    }

    // Clean up timer on unmount or when connection status changes
    return () => {
      if (reconnectTimer) {
        clearTimeout(reconnectTimer);
      }
    };
  }, [isConnected, reconnectAttempt, ip]);

  // If no IP is provided, show an error message
  if (!ip) {
    return (
      <Box
        width="100%"
        height="100vh"
        display="flex"
        alignItems="center"
        justifyContent="center"
        bg="rgba(127, 127, 127, .6)"
      >
        <Box
          p={8}
          maxWidth="800px"
          borderWidth={1}
          borderRadius="lg"
          boxShadow="lg"
          bg="rgba(127, 127, 127, .6)"
          borderColor={borderColor}
        >
          <Heading as="h2" size="xl" mb={4} color={textColor}>
            Error
          </Heading>
          <Text fontSize="lg" color={textColor}>
            No IP address provided. Please specify an IP address.
          </Text>
        </Box>
      </Box>
    );
  }

  return (
    <Box
      width="100%"
      height="100vh"
      display="flex"
      alignItems="center"
      justifyContent="left"
      bg="rgba(127, 127, 127, .00)"
      position="relative"
    >
      <Text 
        fontSize="20px" 
        fontWeight="bold" 
        color="white"
        sx={
          {"text-shadow": "-2px -2px 0 #000, 2px -2px 0 #000, -2px 2px 0 #000, 2px 2px 0 #000;"}
        }
        textAlign="left"
        dangerouslySetInnerHTML={{ __html: message }}
      />
    </Box>
  );
};

export default TranscribePage;
