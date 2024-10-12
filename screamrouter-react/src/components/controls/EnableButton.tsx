import React from 'react';
import { ActionButton } from '../../utils/commonUtils';

interface EnableButtonProps {
  isEnabled: boolean;
  onClick: () => void;
}

const EnableButton: React.FC<EnableButtonProps> = ({ isEnabled, onClick }) => (
  <ActionButton 
    onClick={onClick}
    className={isEnabled ? 'enabled' : 'disabled'}
  >
    {isEnabled ? 'Enabled' : 'Disabled'}
  </ActionButton>
);

export default EnableButton;
