/**
 * React component for a confirmation modal.
 * This component displays a modal dialog that prompts the user to confirm or cancel an action.
 * It includes a message and two buttons: one for confirming the action and another for canceling it.
 *
 * @param {ConfirmationModalProps} props - The properties for the component.
 * @param {boolean} props.isOpen - Indicates whether the modal is currently open.
 * @param {() => void} props.onClose - Callback function to close the modal without taking any action.
 * @param {() => void} props.onConfirm - Callback function to confirm the action and close the modal.
 * @param {string} props.message - The message to display in the modal, explaining the action to be confirmed.
 */
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
