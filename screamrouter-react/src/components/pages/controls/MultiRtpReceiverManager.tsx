import React, { useState, useEffect } from 'react';
import {
  Box,
  Button,
  Table,
  Thead,
  Tbody,
  Tr,
  Th,
  Td,
  Select,
  IconButton,
  useColorModeValue,
  TableContainer,
  Text
} from '@chakra-ui/react';
import { AddIcon, DeleteIcon } from '@chakra-ui/icons';
import axios from 'axios';
import ChannelMappingSelector from './ChannelMappingSelector';
import ApiService, { Sink } from '../../../api/api';

export interface RtpReceiverMapping {
  receiver_sink_name: string;  // Changed from receiver_sink to match backend
  left_channel: number;
  right_channel: number;
}

interface MultiRtpReceiverManagerProps {
  receivers: RtpReceiverMapping[];
  onReceiversChange: (receivers: RtpReceiverMapping[]) => void;
}

const MultiRtpReceiverManager: React.FC<MultiRtpReceiverManagerProps> = ({
  receivers,
  onReceiversChange,
}) => {
  const [compatibleSinks, setCompatibleSinks] = useState<Array<Sink | string>>([]);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);

  // Color values for light/dark mode
  const bgColor = useColorModeValue('gray.50', 'gray.700');
  const borderColor = useColorModeValue('gray.200', 'gray.600');
  const headerBg = useColorModeValue('gray.100', 'gray.600');

  // Fetch all RTP sinks (both enabled and disabled) on component mount
  useEffect(() => {
    const fetchRtpSinks = async () => {
      try {
        setLoading(true);
        // Fetch all sinks and filter for RTP protocol locally
        const response = await ApiService.getSinks();
        const allSinks = Object.values(response.data);
        
        // Filter for RTP sinks (both enabled and disabled)
        const rtpSinks = allSinks.filter(sink => sink.protocol === 'rtp');
        
        setCompatibleSinks(rtpSinks);
        setError(null);
      } catch (err) {
        console.error('Error fetching RTP sinks:', err);
        setError('Failed to fetch RTP sinks');
        setCompatibleSinks([]);
      } finally {
        setLoading(false);
      }
    };

    fetchRtpSinks();
  }, []);

  const handleAddReceiver = () => {
    // Get the first sink name, handling both string and object formats
    const firstSink = compatibleSinks[0];
    const sinkName = typeof firstSink === 'string' ? firstSink : firstSink?.name || '';
    
    const newReceiver: RtpReceiverMapping = {
      receiver_sink_name: sinkName,  // Changed field name
      left_channel: 0,  // FL
      right_channel: 1,  // FR
    };
    onReceiversChange([...receivers, newReceiver]);
  };

  const handleRemoveReceiver = (index: number) => {
    const updatedReceivers = receivers.filter((_, i) => i !== index);
    onReceiversChange(updatedReceivers);
  };

  const handleReceiverSinkChange = (index: number, value: string) => {
    const updatedReceivers = [...receivers];
    updatedReceivers[index].receiver_sink_name = value;  // Changed field name
    onReceiversChange(updatedReceivers);
  };

  const handleChannelChange = (index: number, left: number, right: number) => {
    const updatedReceivers = [...receivers];
    updatedReceivers[index].left_channel = left;
    updatedReceivers[index].right_channel = right;
    onReceiversChange(updatedReceivers);
  };

  if (loading) {
    return <Text>Loading RTP sinks...</Text>;
  }

  if (error) {
    return <Text color="red.500">{error}</Text>;
  }

  if (compatibleSinks.length === 0) {
    return (
      <Box p={4} borderWidth="1px" borderRadius="md" borderColor={borderColor}>
        <Text>No RTP sinks available. Please configure RTP sinks with protocol set to "rtp" first.</Text>
      </Box>
    );
  }

  return (
    <Box>
      <TableContainer>
        <Table variant="simple" size="sm">
          <Thead bg={headerBg}>
            <Tr>
              <Th>Receiver Sink</Th>
              <Th>Channel Mapping</Th>
              <Th width="100px">Actions</Th>
            </Tr>
          </Thead>
          <Tbody>
            {receivers.map((receiver, index) => (
              <Tr key={index} bg={bgColor}>
                <Td>
                  <Select
                    value={receiver.receiver_sink_name}  // Changed field name
                    onChange={(e) => handleReceiverSinkChange(index, e.target.value)}
                    size="sm"
                  >
                    <option value="">Select a sink...</option>
                    {compatibleSinks.map((sink) => {
                      // Handle both string and object formats
                      const sinkName = typeof sink === 'string' ? sink : sink.name;
                      const isEnabled = typeof sink === 'object' ? sink.enabled : true;
                      return (
                        <option key={sinkName} value={sinkName}>
                          {sinkName}{!isEnabled ? ' (disabled)' : ''}
                        </option>
                      );
                    })}
                  </Select>
                </Td>
                <Td>
                  <ChannelMappingSelector
                    leftChannel={receiver.left_channel}
                    rightChannel={receiver.right_channel}
                    onChannelChange={(left, right) => handleChannelChange(index, left, right)}
                  />
                </Td>
                <Td>
                  <IconButton
                    aria-label="Delete receiver"
                    icon={<DeleteIcon />}
                    size="sm"
                    colorScheme="red"
                    variant="ghost"
                    onClick={() => handleRemoveReceiver(index)}
                  />
                </Td>
              </Tr>
            ))}
          </Tbody>
        </Table>
      </TableContainer>
      
      <Button
        leftIcon={<AddIcon />}
        colorScheme="blue"
        size="sm"
        mt={4}
        onClick={handleAddReceiver}
      >
        Add Receiver
      </Button>
    </Box>
  );
};

export default MultiRtpReceiverManager;