import React, { useState, useEffect } from 'react';
import { Box, Text, Badge, VStack } from '@chakra-ui/react';

/**
 * Component to display the WebSocket connection status and recent updates.
 * This is a development component to help verify WebSocket functionality.
 */
const WebSocketStatus: React.FC = () => {
  const [isConnected, setIsConnected] = useState<boolean>(false);
  const [lastUpdate, setLastUpdate] = useState<Date | null>(null);
  const [lastMessage, setLastMessage] = useState<string>('None');
  
  // Monitor WebSocket connections
  useEffect(() => {
    const originalWebSocket = window.WebSocket;
    let wsInstances: WebSocket[] = [];
    
    // Override WebSocket to track connection status
    class MonitoredWebSocket extends originalWebSocket {
      constructor(url: string | URL, protocols?: string | string[]) {
        super(url, protocols);
        
        // Add this instance to the tracking array instead of aliasing 'this'
        wsInstances.push(this);
        
        this.addEventListener('open', () => {
          console.log('WebSocket connected');
          setIsConnected(true);
        });
        
        this.addEventListener('close', () => {
          console.log('WebSocket disconnected');
          setIsConnected(false);
          // Remove from instances array when closed
          wsInstances = wsInstances.filter(ws => ws !== this);
        });
        
        this.addEventListener('message', (event) => {
          setLastUpdate(new Date());
          try {
            const data = JSON.parse(event.data);
            const sources = data.sources ? Object.keys(data.sources).length : 0;
            const sinks = data.sinks ? Object.keys(data.sinks).length : 0;
            const routes = data.routes ? Object.keys(data.routes).length : 0;
            
            setLastMessage(
              `Sources: ${sources}, Sinks: ${sinks}, Routes: ${routes}`
            );
          } catch {
            // Empty catch block - we're just setting a default message on error
            setLastMessage('Error parsing message');
          }
        });
      }
    }
    
    // Replace WebSocket with monitored version
    // Using a proper type cast to fix the "unexpected any" warning
    window.WebSocket = MonitoredWebSocket as typeof WebSocket;
    
    // Clean up on unmount
    return () => {
      window.WebSocket = originalWebSocket;
      for (const ws of wsInstances) {
        if (ws.readyState === WebSocket.OPEN) {
          console.log('Cleaning up monitored WebSocket');
        }
      }
    };
  }, []);
  
  return (
    <Box 
      position="fixed"
      bottom="10px"
      right="10px"
      bg="gray.800"
      color="white"
      p={2}
      borderRadius="md"
      opacity={0.8}
      zIndex={9999}
      maxW="300px"
    >
      <VStack align="start" spacing={1}>
        <Box display="flex" alignItems="center">
          <Text mr={2}>WebSocket:</Text>
          <Badge colorScheme={isConnected ? 'green' : 'red'}>
            {isConnected ? 'Connected' : 'Disconnected'}
          </Badge>
        </Box>
        <Text fontSize="sm">Last update: {lastUpdate ? lastUpdate.toLocaleTimeString() : 'None'}</Text>
        <Text fontSize="xs" isTruncated>Latest: {lastMessage}</Text>
      </VStack>
    </Box>
  );
};

export default WebSocketStatus;