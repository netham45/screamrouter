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
  useColorModeValue,
} from '@chakra-ui/react';
import { ChevronDownIcon, RepeatIcon } from '@chakra-ui/icons';
import { FiServer } from 'react-icons/fi';
import { useRouterInstances, RouterInstance } from '../../hooks/useRouterInstances';

export interface InstanceSwitcherProps {
  buttonProps?: ButtonProps;
  menuListProps?: MenuListProps;
  size?: ButtonProps['size'];
  hideLabel?: boolean;
  showIcon?: boolean;
  resolveHref?: (instance: RouterInstance) => string;
}

const InstanceSwitcher: React.FC<InstanceSwitcherProps> = ({
  buttonProps,
  menuListProps,
  size = 'sm',
  hideLabel = false,
  showIcon = true,
  resolveHref,
}) => {
  const { instances, loading, error, refresh } = useRouterInstances();
  const buttonColorScheme = useColorModeValue('gray', 'whiteAlpha');
  const primaryTextColor = useColorModeValue('gray.900', 'whiteAlpha.900');
  const buttonLabelColor = 'whiteAlpha.900';
  const subTextColor = useColorModeValue('gray.700', 'gray.400');
  const errorTextColor = useColorModeValue('red.500', 'red.300');
  const disabledTextColor = useColorModeValue('gray.700', 'gray.300');
  const currentInstance = instances.find(instance => instance.isCurrent) || null;
  const otherInstances = instances.filter(instance => !instance.isCurrent);

  const buttonLabel = hideLabel
    ? ''
    : (currentInstance?.label ? `Connected: ${currentInstance.label}` : 'Switch Instance');

  const defaultButtonProps: ButtonProps = {
    variant: 'outline',
    colorScheme: buttonColorScheme,
    size,
    px: 3,
  };

  const handleSelect = (instance: RouterInstance) => {
    if (instance.isCurrent) {
      return;
    }
    const targetUrl = resolveHref ? resolveHref(instance) : instance.url;
    if (!targetUrl) {
      return;
    }
    window.location.href = targetUrl;
  };

  const handleRefresh = (event: React.MouseEvent) => {
    event.preventDefault();
    event.stopPropagation();
    void refresh();
  };

  return (
    <Menu placement="bottom-end">
      <MenuButton style={{"margin":"5px"}}
        as={Button}
        {...defaultButtonProps}
        {...buttonProps}
      >
        <HStack spacing={2}>
          {showIcon && <Icon as={FiServer} />}
          {!hideLabel && (
            <Text whiteSpace="nowrap" color={buttonLabelColor}>
              {buttonLabel}
            </Text>
          )}
          {loading ? <Spinner size="xs" /> : <ChevronDownIcon />}
        </HStack>
      </MenuButton>
      <MenuList minW="260px" {...menuListProps}>
        {error && (
          <MenuItem isDisabled color={errorTextColor}>
            {error}
          </MenuItem>
        )}
        {currentInstance && (
          <MenuItem icon={<Icon as={FiServer} />} isDisabled>
            <Box>
              <Text fontWeight="semibold" color={primaryTextColor}>{currentInstance.label}</Text>
              <Text fontSize="xs" color={subTextColor}>
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
              onClick={() => handleSelect(instance)}
            >
              <Box>
                <Text fontWeight="semibold" color={primaryTextColor}>{instance.label}</Text>
                <Text fontSize="xs" color={subTextColor}>
                  {instance.hostname}:{instance.port}
                  {instance.address ? ` · ${instance.address}` : ''}
                </Text>
              </Box>
            </MenuItem>
          ))
        ) : (
          <MenuItem isDisabled color={disabledTextColor}>
            No other instances discovered
          </MenuItem>
        )}
      </MenuList>
    </Menu>
  );
};

export default InstanceSwitcher;
