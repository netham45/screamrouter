import React, { useEffect, useMemo, useRef, useState } from 'react';
import { createPortal } from 'react-dom';
import StepTooltip from './StepTooltip';
import SpotlightPortal from './SpotlightPortal';
import { TutorialStep } from './tutorialSteps';
import { useMdnsDiscovery } from '../../context/MdnsDiscoveryContext';

interface TutorialOverlayProps {
  isLoading: boolean;
  isActive: boolean;
  step: TutorialStep | null;
  stepIndex: number;
  totalSteps: number;
  onNext: () => void;
  onPrevious: () => void;
  onSkip: () => void;
  onSkipToStep?: (stepId: string) => void;
  onRestart: () => void;
  shouldDisplay: boolean;
}

const TutorialOverlay: React.FC<TutorialOverlayProps> = ({
  isLoading,
  isActive,
  step,
  stepIndex,
  totalSteps,
  onNext,
  onPrevious,
  onSkip,
  onSkipToStep,
  onRestart,
  shouldDisplay,
}) => {
  const { openModal } = useMdnsDiscovery();
  const [targetRect, setTargetRect] = useState<DOMRect | null>(null);
  const [waitingForTarget, setWaitingForTarget] = useState(false);
  const observerRef = useRef<MutationObserver | null>(null);
  const lastTargetIdRef = useRef<string | undefined>(undefined);

  const detachObserver = () => {
    if (observerRef.current) {
      observerRef.current.disconnect();
      observerRef.current = null;
    }
  };

  useEffect(() => {
    if (typeof window === 'undefined') {
      return;
    }
    if (!step || !step.targetId || !isActive || !shouldDisplay || isLoading) {
      setTargetRect(null);
      setWaitingForTarget(Boolean(step?.targetId));
      detachObserver();
      return;
    }

    const updateRect = () => {
      if (!step.targetId) {
        setTargetRect(null);
        setWaitingForTarget(false);
        return;
      }

      const element = document.querySelector<HTMLElement>(`[data-tutorial-id="${step.targetId}"]`);
      if (element) {
        const baseRect = element.getBoundingClientRect();
        const interactiveSelectors =
          'a, button, input:not([type="hidden"]), select, textarea, [role="button"], [role="menuitem"], [role="option"], [role="slider"], [tabindex]:not([tabindex="-1"])';

        let top = baseRect.top;
        let left = baseRect.left;
        let right = baseRect.right;
        let bottom = baseRect.bottom;

        element.querySelectorAll<HTMLElement>(interactiveSelectors).forEach(interactive => {
          const childRect = interactive.getBoundingClientRect();
          top = Math.min(top, childRect.top);
          left = Math.min(left, childRect.left);
          right = Math.max(right, childRect.right);
          bottom = Math.max(bottom, childRect.bottom);
        });

        setTargetRect(new DOMRect(left, top, right - left, bottom - top));
        setWaitingForTarget(false);
      } else {
        setTargetRect(null);
        setWaitingForTarget(true);
      }
    };

    updateRect();

    const observer = new MutationObserver(updateRect);
    observer.observe(document.body, {
      attributes: true,
      childList: true,
      subtree: true,
    });
    observerRef.current = observer;

    const handleWindowChange = () => updateRect();
    window.addEventListener('resize', handleWindowChange);
    window.addEventListener('scroll', handleWindowChange, true);

    return () => {
      observer.disconnect();
      window.removeEventListener('resize', handleWindowChange);
      window.removeEventListener('scroll', handleWindowChange, true);
    };
  }, [isActive, isLoading, shouldDisplay, step]);

  useEffect(() => {
    if (typeof window === 'undefined') {
      return;
    }
    if (!step || !step.targetId || !shouldDisplay || !isActive) {
      return;
    }
    if (step.targetId === lastTargetIdRef.current) {
      return;
    }
    lastTargetIdRef.current = step.targetId;
    const element = document.querySelector<HTMLElement>(`[data-tutorial-id="${step.targetId}"]`);
    if (element) {
      element.scrollIntoView({ behavior: 'smooth', block: 'center' });
    }
  }, [isActive, shouldDisplay, step]);

  useEffect(() => {
    if (typeof window === 'undefined') {
      return;
    }
    if (!shouldDisplay || !isActive) {
      return;
    }
    const handleKeydown = (event: KeyboardEvent) => {
      if (event.key === 'Escape') {
        onSkip();
      }
    };
    window.addEventListener('keydown', handleKeydown);
    return () => {
      window.removeEventListener('keydown', handleKeydown);
    };
  }, [isActive, onSkip, shouldDisplay]);

  // Create overlay divs that block pointer events everywhere except the spotlight area
  const overlayDivs = useMemo(() => {
    if (!step || !step.targetId || !targetRect || typeof window === 'undefined') {
      // Full screen overlay when no target
      return [
        <div
          key="full-overlay"
          style={{
            position: 'fixed',
            inset: 0,
            background: 'rgba(0, 0, 0, 0.55)',
            pointerEvents: 'auto',
            zIndex: 997,
          }}
        />
      ];
    }

    const padding = step.spotlightPadding ?? 12;
    const viewportWidth = window.innerWidth;
    const viewportHeight = window.innerHeight;
    const expandedTop = Math.max(0, targetRect.top - padding);
    const expandedLeft = Math.max(0, targetRect.left - padding);
    const expandedRight = Math.min(viewportWidth, targetRect.right + padding);
    const expandedBottom = Math.min(viewportHeight, targetRect.bottom + padding);

    const baseStyle: React.CSSProperties = {
      position: 'fixed',
      background: 'rgba(0, 0, 0, 0.55)',
      pointerEvents: 'auto',
      zIndex: 100,
    };

    

    // Create 4 divs for top, right, bottom, left areas around the spotlight
    // Adjust dimensions to prevent overlap in corners
    return [
      // Top - full width
      <div
        key="overlay-top"
        style={{
          ...baseStyle,
          top: 0,
          left: 0,
          right: 0,
          height: expandedTop,
        }}
      />,
      // Right - from top of spotlight to bottom of viewport
      <div
        key="overlay-right"
        style={{
          ...baseStyle,
          top: expandedTop,
          left: expandedRight,
          right: 0,
          height: viewportHeight - expandedTop,
        }}
      />,
      // Bottom - only from left edge to left of spotlight
      <div
        key="overlay-bottom"
        style={{
          ...baseStyle,
          top: expandedBottom,
          left: 0,
          width: expandedRight,
          bottom: 0,
        }}
      />,
      // Left - only the height of the spotlight
      <div
        key="overlay-left"
        style={{
          ...baseStyle,
          top: expandedTop,
          left: 0,
          width: expandedLeft,
          height: expandedBottom - expandedTop,
        }}
      />,
    ];
  }, [step, targetRect]);

  // Early return AFTER all hooks
  if (!step || !isActive || isLoading || !shouldDisplay) {
    return null;
  }

  const overlay = (
    <>
      {(!waitingForTarget &&
      <>
        {overlayDivs}
        <SpotlightPortal
          rect={targetRect}
          padding={step.spotlightPadding ?? 12}
          visible={Boolean(step.targetId && targetRect)}
        />
        
        <StepTooltip
          step={step}
          stepIndex={stepIndex}
          totalSteps={totalSteps}
          targetRect={targetRect}
          waitingForTarget={waitingForTarget}
          onNext={onNext}
          onPrevious={onPrevious}
          onSkip={onSkip}
          onSkipToStep={onSkipToStep}
          onRestart={onRestart}
          onOpenMdns={step.showMdnsButton ? () => openModal(step.mdnsFilter) : undefined}
        />
      </>)}
    </>
  );

  return createPortal(overlay, document.body);
};

export default TutorialOverlay;
