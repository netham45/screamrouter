import React, { useEffect, useRef, useState } from 'react';
import { Box, Flex, Alert, AlertIcon, AlertTitle, useColorModeValue } from '@chakra-ui/react';
import ApiService, { Source } from '../../api/api';

/**
 * Props for the VNC component
 * @interface VNCProps
 * @property {Source} source - The source for which to display the VNC connection
 * @property {() => void} onClose - Function to call when closing the VNC view
 */
interface VNCProps {
  source: Source;
  onClose?: () => void;
}

/**
 * VNC component for displaying a Virtual Network Computing connection
 * @param {VNCProps} props - The component props
 * @returns {React.FC} A functional component for displaying VNC
 */
const VNC: React.FC<VNCProps> = ({ source }) => {
  const outerIframeRef = useRef<HTMLIFrameElement>(null);
  const [error, setError] = useState<string | null>(null);

  /**
   * Handles errors that may occur when loading the VNC iframe
   * @param {React.SyntheticEvent<HTMLIFrameElement, Event>} event - The error event
   */
  const handleIframeError = (event: React.SyntheticEvent<HTMLIFrameElement, Event>) => {
    console.error('Error loading VNC iframe:', event);
    setError('Failed to load VNC connection. Please try again.');
  };

  const vncUrl = ApiService.getVncUrl(source.name);
  useEffect(() => {
    document.title = `ScreamRouter - ${source.name}`;
  }, [source.name]);

  useEffect(() => {
    document.body.style.overflow = 'hidden';
  }, [source.name]);

  // Define colors based on color mode
  const bgColor = useColorModeValue('gray.50', 'gray.900');

  return (
    <Flex
      direction="column"
      height="100vh"
      width="100%"
      bg={bgColor}
      position="relative"
    >
      {error && (
        <Alert status="error" mb={4}>
          <AlertIcon />
          <AlertTitle>{error}</AlertTitle>
        </Alert>
      )}
      
      <Box
        as="iframe"
        ref={outerIframeRef}
        src={vncUrl}
        title={`${source.name}`}
        flex="1"
        border="none"
        width="100%"
        maxHeight={{base: "90%", md: "100%"}}
        onError={handleIframeError}
      />
    </Flex>
  );
};

export default VNC;