/**
 * React component for displaying transcription data from a WebSocket connection.
 * It connects to a WebSocket endpoint and displays the most recent message received.
 * Uses Chakra UI components for consistent styling.
 */
import React, { useEffect, useState } from 'react';
import { Box, Text, Heading, useColorModeValue, IconButton } from '@chakra-ui/react';
import { useSearchParams, useParams } from 'react-router-dom';
import { CloseIcon } from '@chakra-ui/icons';

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

  // Colors for light/dark mode
  const textColor = useColorModeValue('gray.800', 'gray.100');
  const borderColor = useColorModeValue('gray.200', 'gray.700');

  useEffect(() => {
    // Set document title
    document.title = `ScreamRouter - Transcribe`;

    // Return early if no IP is provided
    if (!ip) {
      setMessage('Error: No IP address provided');
      return;
    }

    // Create WebSocket connection
    const wsUrl = `ws://127.0.0.1:8085/transcribe/${ip}/`;
    const ws = new WebSocket(wsUrl);

    // WebSocket event handlers
    ws.onopen = () => {
      console.log('WebSocket connection established');
      setMessage('Connected. Waiting for transcription data...');
    };

    ws.onmessage = (event) => {
      console.log('WebSocket message received:', event.data);
      // Replace newlines with <br/> tags
      const formattedMessage = event.data.replace(/\n/g, '<br/>');
      setMessage(formattedMessage);
    };

    ws.onerror = (error) => {
      console.error('WebSocket error:', error);
      setMessage(`Error connecting to transcription service. Please try again.`);
    };

    ws.onclose = () => {
      console.log('WebSocket connection closed');
      setMessage('Connection closed. Refresh to reconnect.');
    };

    // Clean up WebSocket connection on component unmount
    return () => {
      if (ws && ws.readyState === WebSocket.OPEN) {
        ws.close();
      }
    };
  }, [ip]);

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

  // Function to close the window
  const handleClose = () => {
    window.close();
  };

  return (
    <Box
      width="100%"
      height="100vh"
      display="flex"
      alignItems="center"
      justifyContent="center"
      bg="rgba(127, 127, 127, .05)"
      position="relative"
    >
      {/* Close button */}
      <IconButton
        aria-label="Close"
        icon={<CloseIcon />}
        size="md"
        position="absolute"
        top="10px"
        right="10px"
        onClick={handleClose}
        bg="transparent"
        color="white"
        _hover={{ bg: 'rgba(255, 255, 255, 0.1)' }}
      />
      
      <Text 
        fontSize="20px" 
        fontWeight="bold" 
        color="white"
        sx={
          {"text-shadow": "-2px -2px 0 #000, 2px -2px 0 #000, -2px 2px 0 #000, 2px 2px 0 #000;"}
        }
        textAlign="center"
        dangerouslySetInnerHTML={{ __html: message }}
      />
    </Box>
  );
};

export default TranscribePage;
