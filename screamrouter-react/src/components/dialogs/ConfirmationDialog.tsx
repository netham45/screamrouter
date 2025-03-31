/**
 * Confirmation dialog component.
 * Used for confirming potentially destructive actions.
 */
import React from 'react';
import {
  AlertDialog,
  AlertDialogBody,
  AlertDialogFooter,
  AlertDialogHeader,
  AlertDialogContent,
  AlertDialogOverlay,
  Button
} from '@chakra-ui/react';

interface ConfirmationDialogProps {
  /**
   * Whether the dialog is open
   */
  isOpen: boolean;
  
  /**
   * Function to close the dialog
   */
  onClose: () => void;
  
  /**
   * Function to confirm the action
   */
  onConfirm: () => void;
  
  /**
   * Title of the dialog
   */
  title: string;
  
  /**
   * Message to display in the dialog
   */
  message: string;
  
  /**
   * Text for the confirm button
   */
  confirmButtonText?: string;
  
  /**
   * Color scheme for the confirm button
   */
  confirmColorScheme?: string;
}

/**
 * A reusable confirmation dialog component.
 */
const ConfirmationDialog: React.FC<ConfirmationDialogProps> = ({
  isOpen,
  onClose,
  onConfirm,
  title,
  message,
  confirmButtonText = 'Delete',
  confirmColorScheme = 'red'
}) => {
  const cancelRef = React.useRef<HTMLButtonElement>(null);
  
  const handleConfirm = () => {
    onConfirm();
    onClose();
  };
  
  return (
    <AlertDialog
      isOpen={isOpen}
      leastDestructiveRef={cancelRef}
      onClose={onClose}
    >
      <AlertDialogOverlay 
        bg="rgba(0, 0, 0, 0)"
      >
        <AlertDialogContent
          pointerEvents="auto"
          position="fixed"
          bottom="30%"
          left="50%"
          transform="translateX(-50%)"
        >
          <AlertDialogHeader fontSize="lg" fontWeight="bold">
            {title}
          </AlertDialogHeader>

          <AlertDialogBody>
            {message}
          </AlertDialogBody>

          <AlertDialogFooter>
            <Button ref={cancelRef} onClick={onClose}>
              Cancel
            </Button>
            <Button colorScheme={confirmColorScheme} onClick={handleConfirm} ml={3}>
              {confirmButtonText}
            </Button>
          </AlertDialogFooter>
        </AlertDialogContent>
      </AlertDialogOverlay>
    </AlertDialog>
  );
};

export default ConfirmationDialog;
