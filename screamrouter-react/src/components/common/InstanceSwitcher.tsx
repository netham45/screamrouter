import React from 'react';
import {
  Box,
  Button,
  ButtonProps,
  HStack,
  Icon,
  Menu,
  MenuButton,
  MenuDivider,
  MenuItem,
  MenuList,
  MenuListProps,
  Spinner,
  Text,
  Tooltip,
} from '@chakra-ui/react';
import { ChevronDownIcon, RepeatIcon } from '@chakra-ui/icons';
import { FiServer } from 'react-icons/fi';
import { useRouterInstances } from '../../hooks/useRouterInstances';

export interface InstanceSwitcherProps {
  buttonProps?: ButtonProps;
  menuListProps?: MenuListProps;
  size?: ButtonProps['size'];
  hideLabel?: boolean;
  showIcon?: boolean;
}

const InstanceSwitcher: React.FC<InstanceSwitcherProps> = ({
  buttonProps,
  menuListProps,
  size = 'sm',
  hideLabel = false,
  showIcon = true,
}) => {
  const { instances, loading, error, refresh } = useRouterInstances();
  const currentInstance = instances.find(instance => instance.isCurrent) || null;
  const otherInstances = instances.filter(instance => !instance.isCurrent);

  const buttonLabel = hideLabel
    ? ''
    : (currentInstance?.label ? `Connected: ${currentInstance.label}` : 'Switch Instance');

  const defaultButtonProps: ButtonProps = {
    variant: 'outline',
    colorScheme: 'whiteAlpha',
    size,
    px: 3,
  };

  const handleSelect = (url: string, isCurrent: boolean) => {
    if (!url || isCurrent) {
      return;
    }
    window.location.href = url;
  };

  const handleRefresh = (event: React.MouseEvent) => {
    event.preventDefault();
    event.stopPropagation();
    void refresh();
  };

  return (
    <Menu placement="bottom-end">
      <Tooltip label="Switch between discovered ScreamRouter instances" hasArrow>
        <MenuButton
          as={Button}
          {...defaultButtonProps}
          {...buttonProps}
        >
          <HStack spacing={2}>
            {showIcon && <Icon as={FiServer} />}
            {!hideLabel && (
              <Text whiteSpace="nowrap">
                {loading ? 'Discovering…' : buttonLabel}
              </Text>
            )}
            {loading ? <Spinner size="xs" /> : <ChevronDownIcon />}
          </HStack>
        </MenuButton>
      </Tooltip>
      <MenuList minW="260px" {...menuListProps}>
        {error && (
          <MenuItem isDisabled color="red.400">
            {error}
          </MenuItem>
        )}
        {currentInstance && (
          <MenuItem icon={<Icon as={FiServer} />} isDisabled>
            <Box>
              <Text fontWeight="semibold">{currentInstance.label}</Text>
              <Text fontSize="xs" color="gray.500">
                This instance · {currentInstance.hostname}
              </Text>
            </Box>
          </MenuItem>
        )}
        {(currentInstance && otherInstances.length > 0) && <MenuDivider />}
        {otherInstances.length > 0 ? (
          otherInstances.map(instance => (
            <MenuItem
              key={instance.id}
              icon={<Icon as={FiServer} />}
              onClick={() => handleSelect(instance.url, instance.isCurrent)}
            >
              <Box>
                <Text fontWeight="semibold">{instance.label}</Text>
                <Text fontSize="xs" color="gray.500">
                  {instance.hostname}:{instance.port}
                  {instance.address ? ` · ${instance.address}` : ''}
                </Text>
              </Box>
            </MenuItem>
          ))
        ) : (
          <MenuItem isDisabled>
            No other instances discovered
          </MenuItem>
        )}
        <MenuDivider />
        <MenuItem icon={<RepeatIcon />} onClick={handleRefresh}>
          Refresh discovery
        </MenuItem>
      </MenuList>
    </Menu>
  );
};

export default InstanceSwitcher;
