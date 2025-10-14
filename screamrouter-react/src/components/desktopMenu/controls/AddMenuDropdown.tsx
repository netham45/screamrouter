/**
 * AddMenuDropdown component for the DesktopMenu.
 * Provides a dropdown menu with options to add sources, sinks, routes, and groups.
 */
import React from 'react';
import {
  Menu,
  MenuButton,
  MenuList,
  MenuItem,
  Button,
  Icon
} from '@chakra-ui/react';
import { AddIcon } from '@chakra-ui/icons';
import { colorContextInstance } from '../context/ColorContext';

interface AddMenuDropdownProps {
  buttonBgInactive: string;
  buttonTextInactive: string;
  onAddSource: () => void;
  onAddSourceGroup: () => void;
  onAddSink: () => void;
  onAddSinkGroup: () => void;
  onAddRoute: () => void;
}

const AddMenuDropdown: React.FC<AddMenuDropdownProps> = ({
  buttonBgInactive,
  buttonTextInactive,
  onAddSource,
  onAddSourceGroup,
  onAddSink,
  onAddSinkGroup,
  onAddRoute
}) => {
  // Use the same color system as the main menu
  const menuBgColor = colorContextInstance.getDarkerColor(.85, .9);
  const menuHoverColor = colorContextInstance.getDarkerColor(.75, .85);
  const borderColor = colorContextInstance.getDarkerColor(.7, .8);

  return (
    <Menu placement="top">
      <MenuButton
        as={Button}
        variant="outline"
        style={{
          backgroundColor: buttonBgInactive,
          color: buttonTextInactive,
          borderColor: 'rgba(255, 255, 255, 0.16)',
          borderWidth: '1px'
        }}
        _hover={{ opacity: 0.8 }}
        _active={{ opacity: 0.9 }}
        size="xs"
        leftIcon={<Icon as={AddIcon} boxSize={3} />}
      >
        Add
      </MenuButton>
      <MenuList
        style={{
          backgroundColor: menuBgColor,
          borderColor: borderColor,
          borderWidth: '1px',
          borderStyle: 'solid'
        }}
        boxShadow="lg"
        minW="160px"
        py={1}
      >
        <MenuItem
          style={{
            backgroundColor: menuBgColor,
            color: '#EEEEEE'
          }}
          _hover={{ 
            backgroundColor: menuHoverColor,
            opacity: 0.9
          }}
          _focus={{ 
            backgroundColor: menuHoverColor,
            opacity: 0.9
          }}
          onClick={onAddSource}
          fontSize="sm"
          py={2}
        >
          Add Source
        </MenuItem>
        <MenuItem
          style={{
            backgroundColor: menuBgColor,
            color: '#EEEEEE'
          }}
          _hover={{ 
            backgroundColor: menuHoverColor,
            opacity: 0.9
          }}
          _focus={{ 
            backgroundColor: menuHoverColor,
            opacity: 0.9
          }}
          onClick={onAddSourceGroup}
          fontSize="sm"
          py={2}
        >
          Add Source Group
        </MenuItem>
        <MenuItem
          style={{
            backgroundColor: menuBgColor,
            color: '#EEEEEE'
          }}
          _hover={{ 
            backgroundColor: menuHoverColor,
            opacity: 0.9
          }}
          _focus={{ 
            backgroundColor: menuHoverColor,
            opacity: 0.9
          }}
          onClick={onAddSink}
          fontSize="sm"
          py={2}
        >
          Add Sink
        </MenuItem>
        <MenuItem
          style={{
            backgroundColor: menuBgColor,
            color: '#EEEEEE'
          }}
          _hover={{ 
            backgroundColor: menuHoverColor,
            opacity: 0.9
          }}
          _focus={{ 
            backgroundColor: menuHoverColor,
            opacity: 0.9
          }}
          onClick={onAddSinkGroup}
          fontSize="sm"
          py={2}
        >
          Add Sink Group
        </MenuItem>
        <MenuItem
          style={{
            backgroundColor: menuBgColor,
            color: '#EEEEEE'
          }}
          _hover={{ 
            backgroundColor: menuHoverColor,
            opacity: 0.9
          }}
          _focus={{ 
            backgroundColor: menuHoverColor,
            opacity: 0.9
          }}
          onClick={onAddRoute}
          fontSize="sm"
          py={2}
        >
          Add Route
        </MenuItem>
      </MenuList>
    </Menu>
  );
};

export default AddMenuDropdown;