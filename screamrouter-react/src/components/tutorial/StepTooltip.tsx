import React, { useLayoutEffect, useMemo, useRef, useState } from 'react';
import {
  AlertDialog,
  AlertDialogBody,
  AlertDialogContent,
  AlertDialogFooter,
  AlertDialogHeader,
  AlertDialogOverlay,
  Badge,
  Box,
  Button,
  Flex,
  Heading,
  Stack,
  Text,
  useColorModeValue,
} from '@chakra-ui/react';
import { TutorialStep } from './tutorialSteps';
import { useTutorial } from '../../context/TutorialContext';
import { useMdnsDiscovery } from '../../context/MdnsDiscoveryContext';

interface StepTooltipProps {
  step: TutorialStep;
  stepIndex: number;
  totalSteps: number;
  targetRect: DOMRect | null;
  waitingForTarget: boolean;
  onNext: () => void;
  onPrevious: () => void;
  onSkip: () => void;
  onSkipToStep?: (stepId: string) => void;
  onRestart: () => void;
  onOpenMdns?: () => void;
}

interface TooltipPosition {
  top: number;
  left: number;
  transform?: string;
}

const StepTooltip: React.FC<StepTooltipProps> = ({
  step,
  stepIndex,
  totalSteps,
  targetRect,
  waitingForTarget,
  onNext,
  onPrevious,
  onSkip,
  onSkipToStep,
  onRestart,
  onOpenMdns,
}) => {
  const ref = useRef<HTMLDivElement>(null);
  const cancelRef = useRef<HTMLButtonElement>(null);
  const [position, setPosition] = useState<TooltipPosition>(() => ({
    top: window.innerHeight / 2,
    left: window.innerWidth / 2,
    transform: 'translate(-50%, -50%)',
  }));
  const [isSkipDialogOpen, setIsSkipDialogOpen] = useState(false);

  const { isStepCompleted } = useTutorial();
  const { isModalOpen: isMdnsModalOpen } = useMdnsDiscovery();

  const isCompleted = useMemo(() => isStepCompleted(step.id), [isStepCompleted, step.id]);
  const isInformational = Boolean(step.isInformational || (!step.targetId && !waitingForTarget));
  const canAdvance = isInformational || isCompleted;

  useLayoutEffect(() => {
    if (typeof window === 'undefined') {
      return;
    }
    const tooltip = ref.current;
    if (!tooltip) return;

    const margin = 16;
    const modalBuffer = 24;
    const { offsetWidth, offsetHeight } = tooltip;

    if (!targetRect) {
      setPosition({
        top: window.innerHeight / 2,
        left: window.innerWidth / 2,
        transform: 'translate(-50%, -50%)',
      });
      return;
    }

    let top = targetRect.bottom + margin;
    let left = targetRect.left + targetRect.width / 2 - offsetWidth / 2;
    let transform: string | undefined;

    switch (step.placement) {
      case 'top':
        top = targetRect.top - offsetHeight - margin;
        left = targetRect.left + targetRect.width / 2 - offsetWidth / 2;
        break;
      case 'left':
        left = targetRect.left - offsetWidth - margin;
        top = targetRect.top + targetRect.height / 2 - offsetHeight / 2;
        break;
      case 'right':
        left = targetRect.right + margin;
        top = targetRect.top + targetRect.height / 2 - offsetHeight / 2;
        break;
      default:
        break;
    }

    const maxTop = window.innerHeight - offsetHeight - margin;
    const maxLeft = window.innerWidth - offsetWidth - margin;

    top = Math.min(Math.max(top, margin), maxTop);
    left = Math.min(Math.max(left, margin), maxLeft);

    if (isMdnsModalOpen) {
      const modalElement = document.querySelector<HTMLElement>('[role="dialog"]');
      if (modalElement) {
        const modalRect = modalElement.getBoundingClientRect();
        const tooltipRect = {
          top,
          bottom: top + offsetHeight,
          left,
          right: left + offsetWidth,
        };
        const overlaps =
          tooltipRect.left < modalRect.right + modalBuffer &&
          tooltipRect.right > modalRect.left - modalBuffer &&
          tooltipRect.top < modalRect.bottom + modalBuffer &&
          tooltipRect.bottom > modalRect.top - modalBuffer;

        if (overlaps) {
          const spaceAbove = modalRect.top - offsetHeight - modalBuffer;
          const spaceBelow = window.innerHeight - modalRect.bottom - offsetHeight - modalBuffer;
          const spaceLeft = modalRect.left - offsetWidth - modalBuffer;
          const spaceRight = window.innerWidth - modalRect.right - offsetWidth - modalBuffer;

          if (spaceAbove >= margin) {
            top = Math.max(margin, modalRect.top - offsetHeight - modalBuffer);
          } else if (spaceBelow >= margin) {
            top = Math.min(maxTop, modalRect.bottom + modalBuffer);
          } else if (spaceLeft >= margin) {
            left = Math.max(margin, modalRect.left - offsetWidth - modalBuffer);
          } else if (spaceRight >= margin) {
            left = Math.min(maxLeft, modalRect.right + modalBuffer);
          } else {
            top = Math.max(margin, modalRect.bottom + modalBuffer);
          }
          transform = undefined;
        }
      }
    }

    setPosition({ top, left, transform });
  }, [isMdnsModalOpen, step.id, step.placement, targetRect, waitingForTarget]);

  const nextButtonLabel = useMemo(() => (stepIndex === totalSteps - 1 ? 'Finish' : 'Next'), [stepIndex, totalSteps]);

  const bgColor = useColorModeValue('white', 'gray.900');
  const textColor = useColorModeValue('gray.800', 'gray.100');
  const descriptionColor = useColorModeValue('gray.700', 'gray.200');
  const subtleText = useColorModeValue('gray.500', 'gray.400');
  const borderColor = useColorModeValue('blackAlpha.200', 'whiteAlpha.200');
  const badgeColor = isCompleted ? 'green' : isInformational ? 'blue' : 'orange';
  const badgeLabel = isCompleted ? 'Completed' : isInformational ? 'Info' : 'In progress';
  const isNextDisabled = (waitingForTarget && Boolean(step.targetId)) || !canAdvance;

  return (
    <>
      <Box
        ref={ref}
        position="fixed"
        top={position.top}
        left={position.left}
        transform={position.transform}
        zIndex={1002}
        maxW="360px"
        pointerEvents="auto"
        bg={bgColor}
        color={textColor}
        borderRadius="md"
        boxShadow="lg"
        borderWidth="1px"
        borderColor={borderColor}
        p={4}
      >
        <Stack spacing={3}>
          <Flex justify="space-between" align="center">
            <Flex align="center" gap={2}>
              <Text fontSize="sm" fontWeight="medium" color={subtleText}>
                Step {stepIndex + 1} of {totalSteps}
              </Text>
              <Badge colorScheme={badgeColor}>{badgeLabel}</Badge>
            </Flex>
            {step.skipSectionTargetId && onSkipToStep ? (
              <Button
                variant="ghost"
                size="xs"
                onClick={() => onSkipToStep(step.skipSectionTargetId || '')}
              >
                {step.skipSectionLabel ?? 'Skip Section'}
              </Button>
            ) : (
              step.allowSkip !== false && (
                <Button variant="ghost" size="xs" onClick={() => setIsSkipDialogOpen(true)}>
                  Skip Tutorial
                </Button>
              )
            )}
          </Flex>
          <Heading as="h3" size="md">
            {step.title}
          </Heading>
          <Text fontSize="sm" color={descriptionColor}>
            {waitingForTarget && step.targetId
              ? 'Waiting for the interface element to appear...'
              : step.description}
          </Text>
          {step.showMdnsButton && onOpenMdns && (
            <Button
              size="sm"
              colorScheme="blue"
              variant="solid"
              onClick={onOpenMdns}
              isDisabled={waitingForTarget}
            >
              Discover on Network
            </Button>
          )}
          <Flex justify="space-between" align="center" pt={2}>
            <Button
              variant="ghost"
              size="sm"
              onClick={onPrevious}
              isDisabled={stepIndex === 0 || Boolean(step.disableBack)}
            >
              Back
            </Button>
            <Stack direction="row" spacing={2}>
              {step.id === 'tutorial-complete' && (
                <Button variant="outline" size="sm" onClick={onRestart}>
                  Restart
                </Button>
              )}
              <Button
                colorScheme="blue"
                size="sm"
                onClick={onNext}
                isDisabled={isNextDisabled}
              >
                {nextButtonLabel}
              </Button>
            </Stack>
          </Flex>
        </Stack>
      </Box>
      <AlertDialog
        isOpen={isSkipDialogOpen}
        leastDestructiveRef={cancelRef}
        onClose={() => setIsSkipDialogOpen(false)}
        isCentered
      >
        <AlertDialogOverlay>
          <AlertDialogContent>
            <AlertDialogHeader fontSize="lg" fontWeight="bold">
              Skip tutorial?
            </AlertDialogHeader>
            <AlertDialogBody>
              Skipping now will mark every step as complete. You can restart the tutorial later from the help menu.
            </AlertDialogBody>
            <AlertDialogFooter>
              <Button ref={cancelRef} onClick={() => setIsSkipDialogOpen(false)}>
                Keep Going
              </Button>
              <Button colorScheme="red" ml={3} onClick={() => { setIsSkipDialogOpen(false); onSkip(); }}>
                Skip Tutorial
              </Button>
            </AlertDialogFooter>
          </AlertDialogContent>
        </AlertDialogOverlay>
      </AlertDialog>
    </>
  );
};

export default StepTooltip;
