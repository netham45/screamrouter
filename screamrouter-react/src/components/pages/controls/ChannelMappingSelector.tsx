import React from 'react';
import { Select, FormControl, FormLabel, Grid, GridItem } from '@chakra-ui/react';

const channelOptions = ['FL', 'FR', 'C', 'LFE', 'BL', 'BR', 'SL', 'SR'];

interface ChannelMappingSelectorProps {
    leftChannel: number;
    rightChannel: number;
    onChannelChange: (left: number, right: number) => void;
}

const ChannelMappingSelector: React.FC<ChannelMappingSelectorProps> = ({
    leftChannel,
    rightChannel,
    onChannelChange,
}) => {
    const handleLeftChannelChange = (event: React.ChangeEvent<HTMLSelectElement>) => {
        onChannelChange(parseInt(event.target.value), rightChannel);
    };

    const handleRightChannelChange = (event: React.ChangeEvent<HTMLSelectElement>) => {
        onChannelChange(leftChannel, parseInt(event.target.value));
    };

    return (
        <Grid templateColumns="repeat(2, 1fr)" gap={4}>
            <GridItem>
                <FormControl>
                    <FormLabel>Left Channel</FormLabel>
                    <Select
                        value={leftChannel}
                        onChange={handleLeftChannelChange}
                    >
                        {channelOptions.map((channel, index) => (
                            <option key={index} value={index}>
                                {channel}
                            </option>
                        ))}
                    </Select>
                </FormControl>
            </GridItem>
            <GridItem>
                <FormControl>
                    <FormLabel>Right Channel</FormLabel>
                    <Select
                        value={rightChannel}
                        onChange={handleRightChannelChange}
                    >
                        {channelOptions.map((channel, index) => (
                            <option key={index} value={index}>
                                {channel}
                            </option>
                        ))}
                    </Select>
                </FormControl>
            </GridItem>
        </Grid>
    );
};

export default ChannelMappingSelector;