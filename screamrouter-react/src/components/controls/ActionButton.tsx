import React from 'react';
import { ActionButton as CommonActionButton } from '../../utils/commonUtils';

interface ActionButtonProps {
  onClick: () => void;
  className?: string;
  children: React.ReactNode;
}

const ActionButton: React.FC<ActionButtonProps> = ({ onClick, className, children }) => (
  <CommonActionButton onClick={onClick} className={className}>
    {children}
  </CommonActionButton>
);

export default ActionButton;
