import React from 'react';
import { ActionButton } from '../../utils/commonUtils';

interface StarButtonProps {
  isStarred: boolean;
  onClick: () => void;
}

const StarButton: React.FC<StarButtonProps> = ({ isStarred, onClick }) => (
  <ActionButton onClick={onClick}>
    {isStarred ? '★' : '☆'}
  </ActionButton>
);

export default StarButton;
