import React from 'react';
import ActionButton from './controls/ActionButton';

interface ConfirmationModalProps {
  isOpen: boolean;
  onClose: () => void;
  onConfirm: () => void;
  message: string;
}

const ConfirmationModal: React.FC<ConfirmationModalProps> = ({ isOpen, onClose, onConfirm, message }) => {
  if (!isOpen) return null;

  return (
    <div className="modal-backdrop">
      <div className="modal-content">
        <h3>Confirm Action</h3>
        <p>{message}</p>
        <div className="modal-actions">
          <ActionButton onClick={onConfirm}>Confirm</ActionButton>
          <ActionButton onClick={onClose}>Cancel</ActionButton>
        </div>
      </div>
    </div>
  );
};

export default ConfirmationModal;