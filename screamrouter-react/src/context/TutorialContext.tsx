import React, {
  createContext,
  useCallback,
  useContext,
  useEffect,
  useMemo,
  useRef,
  useState,
} from 'react';
import {
  AlertDialog,
  AlertDialogBody,
  AlertDialogContent,
  AlertDialogFooter,
  AlertDialogHeader,
  AlertDialogOverlay,
  Button,
} from '@chakra-ui/react';
import ApiService from '../api/api';
import { Preferences, PreferencesUpdatePayload, TutorialProgress } from '../types/preferences';
import TutorialOverlay from '../components/tutorial/TutorialOverlay';
import { tutorialSteps, TUTORIAL_VERSION, TutorialStep } from '../components/tutorial/tutorialSteps';
import { openAddPage } from '../components/fullMenu/utils';
import { useMdnsDiscovery } from './MdnsDiscoveryContext';

interface TutorialContextValue {
  steps: TutorialStep[];
  currentStepIndex: number;
  currentStep: TutorialStep | null;
  isLoading: boolean;
  isActive: boolean;
  progress: TutorialProgress;
  completedSteps: Set<string>;
  startTutorial: () => void;
  restartTutorial: () => void;
  skipTutorial: () => void;
  skipToStep: (stepId: string) => void;
  nextStep: () => void;
  previousStep: () => void;
  completeStep: (stepId: string, autoAdvance?: boolean) => void;
  isStepCompleted: (stepId: string) => boolean;
}

const defaultProgress: TutorialProgress = {
  current_step: 0,
  completed: false,
  completed_at: null,
  version: TUTORIAL_VERSION,
  completed_steps: [],
  completedSteps: [],
};

const CONTINUATION_TARGETS = {
  source: {
    stepId: 'source-ip-input',
    url: '/site/add-source',
  },
  sink: {
    stepId: 'sink-ip-input',
    url: '/site/add-sink',
  },
  route: {
    stepId: 'route-source-input',
    url: '/site/add-route',
  },
} as const;

type ContinuationForm = keyof typeof CONTINUATION_TARGETS;

const isKnownContinuationForm = (value: unknown): value is ContinuationForm =>
  typeof value === 'string' && value in CONTINUATION_TARGETS;

const SUBMIT_STEP_IDS: Record<ContinuationForm, string> = {
  source: 'source-submit',
  sink: 'sink-submit',
  route: 'route-submit',
};

const TutorialContext = createContext<TutorialContextValue | undefined>(undefined);

export const TutorialProvider: React.FC<{ children: React.ReactNode }> = ({ children }) => {
  const steps = tutorialSteps;
  const totalSteps = steps.length;

  const [progress, setProgress] = useState<TutorialProgress>(defaultProgress);
  const [currentStepIndex, setCurrentStepIndex] = useState(0);
  const [isActive, setIsActive] = useState(false);
  const [isLoading, setIsLoading] = useState(true);
  const [initialised, setInitialised] = useState(false);
  const [completedSteps, setCompletedSteps] = useState<Set<string>>(new Set());
  const [continuationForm, setContinuationForm] = useState<ContinuationForm | null>(null);
  const continuationCancelRef = useRef<HTMLButtonElement>(null);
  const formSubmissionPendingRef = useRef(false);
  const formSubmissionResetTimeoutRef = useRef<ReturnType<typeof setTimeout> | null>(null);
  const currentStepIndexRef = useRef(currentStepIndex);
  const completedStepsRef = useRef<Set<string>>(completedSteps);

  const { registerSelectionHandler } = useMdnsDiscovery();
  const isDesktopMode = typeof window !== 'undefined' && window.location.pathname.includes('desktopMenu');

  const currentStep = steps[currentStepIndex] ?? null;

  const applyProgressUpdate = useCallback(async (update: Partial<TutorialProgress>) => {
    const completedUpdate = update.completed_steps ?? update.completedSteps;

    // Optimistically update local state FIRST
    const mergedCompletedSteps = completedUpdate ?? [];
    setProgress(prev => ({
      ...prev,
      ...update,
      version: TUTORIAL_VERSION,
      completed_steps: mergedCompletedSteps,
      completedSteps: mergedCompletedSteps,
    }));
    if (completedUpdate) {
      setCompletedSteps(new Set(completedUpdate));
    }

    const payload: PreferencesUpdatePayload = {
      tutorial: {
        ...update,
        version: TUTORIAL_VERSION,
      },
    };

    if (completedUpdate) {
      payload.tutorial!.completed_steps = completedUpdate;
    }

    try {
      // Persist to backend but don't overwrite local state with response
      // This prevents race conditions where backend returns stale data
      await ApiService.updatePreferences(payload);
    } catch (error) {
      console.error('Failed to persist tutorial preferences', error);
    }
  }, []);

  const resetProgressOnVersionMismatch = useCallback(async () => {
    setProgress(defaultProgress);
    setCompletedSteps(new Set());
    setCurrentStepIndex(0);
    const payload: PreferencesUpdatePayload = {
      tutorial: {
        ...defaultProgress,
      },
    };
    try {
      await ApiService.updatePreferences(payload);
    } catch (error) {
      console.error('Failed to reset tutorial progress', error);
    }
  }, []);

  useEffect(() => {
    let cancelled = false;

    const loadPreferences = async () => {
      setIsLoading(true);
      try {
        const response = await ApiService.getPreferences();
        if (cancelled) return;
        const prefs: Preferences | undefined = response.data;
        const tutorial = prefs?.tutorial;
        if (!tutorial || tutorial.version !== TUTORIAL_VERSION) {
          await resetProgressOnVersionMismatch();
          if (cancelled) return;
          setIsActive(true);
          setInitialised(true);
        } else {
          const completed = tutorial.completed_steps ?? tutorial.completedSteps ?? [];
          setProgress({
            current_step: tutorial.current_step ?? 0,
            completed: Boolean(tutorial.completed),
            completed_at: tutorial.completed_at ?? null,
            version: tutorial.version,
            completed_steps: completed,
            completedSteps: completed,
          });
          setCompletedSteps(new Set(completed));
          setCurrentStepIndex(Math.min(tutorial.current_step ?? 0, totalSteps - 1));
          setIsActive(!tutorial.completed);
          setInitialised(true);
        }
      } catch (error) {
        console.error('Unable to load preferences', error);
        setProgress(defaultProgress);
        setCompletedSteps(new Set());
        setCurrentStepIndex(0);
        setIsActive(true);
        setInitialised(true);
      } finally {
        if (!cancelled) {
          setIsLoading(false);
        }
      }
    };

    loadPreferences();

    return () => {
      cancelled = true;
    };
  }, [resetProgressOnVersionMismatch, totalSteps]);

  useEffect(() => {
    if (!initialised) return;
    setCurrentStepIndex(prev => Math.min(progress.current_step ?? prev, totalSteps - 1));
  }, [initialised, progress.current_step, totalSteps]);

  useEffect(() => {
    currentStepIndexRef.current = currentStepIndex;
  }, [currentStepIndex]);

  useEffect(() => {
    completedStepsRef.current = completedSteps;
  }, [completedSteps]);

  const startTutorial = useCallback(() => {
    setIsActive(true);
    if (progress.completed) {
      setCurrentStepIndex(0);
      setCompletedSteps(new Set());
      void applyProgressUpdate({
        current_step: 0,
        completed: false,
        completed_at: null,
        completed_steps: [],
      });
    }
  }, [applyProgressUpdate, progress.completed]);

  const restartTutorial = useCallback(() => {
    setIsActive(true);
    setCurrentStepIndex(0);
    setCompletedSteps(new Set());
    void applyProgressUpdate({
      current_step: 0,
      completed: false,
      completed_at: null,
      completed_steps: [],
    });
  }, [applyProgressUpdate]);

  const skipTutorial = useCallback(() => {
    setIsActive(false);
    setCurrentStepIndex(totalSteps - 1);
    const allStepIds = steps.map(step => step.id);
    setCompletedSteps(new Set(allStepIds));
    void applyProgressUpdate({
      current_step: totalSteps - 1,
      completed: true,
      completed_steps: allStepIds,
    });
  }, [applyProgressUpdate, steps, totalSteps]);

  const skipToStep = useCallback(
    (targetStepId: string) => {
      const targetIndex = steps.findIndex(step => step.id === targetStepId);
      if (targetIndex === -1) {
        console.warn('Unknown tutorial step:', targetStepId);
        return;
      }

      const currentIndex = currentStepIndexRef.current;

      if (targetIndex <= currentIndex) {
        currentStepIndexRef.current = targetIndex;
        setCurrentStepIndex(targetIndex);
        void applyProgressUpdate({ current_step: targetIndex });
        return;
      }

      const updatedCompletedSteps = new Set(completedStepsRef.current);
      for (let index = currentIndex; index < targetIndex; index += 1) {
        updatedCompletedSteps.add(steps[index].id);
      }

      completedStepsRef.current = updatedCompletedSteps;
      setCompletedSteps(updatedCompletedSteps);

      currentStepIndexRef.current = targetIndex;
      setCurrentStepIndex(targetIndex);

      const completedStepsArray = Array.from(updatedCompletedSteps);
      const allCompleted = steps.every(step => updatedCompletedSteps.has(step.id));
      const updatePayload: Partial<TutorialProgress> = {
        current_step: targetIndex,
        completed_steps: completedStepsArray,
      };

      if (allCompleted) {
        updatePayload.completed = true;
        updatePayload.completed_at = new Date().toISOString();
        setIsActive(false);
      }

      void applyProgressUpdate(updatePayload);
    },
    [applyProgressUpdate, steps]
  );

  useEffect(() => {
    if (typeof window === 'undefined') {
      return;
    }
    const handleMessage = (event: MessageEvent) => {
      if (event.origin !== window.location.origin) {
        return;
      }
      const data = event.data;
      if (!data || typeof data !== 'object') {
        return;
      }
      const { type, form, resourceType } = data as {
        type?: unknown;
        form?: unknown;
        resourceType?: unknown;
      };

      if (type === 'RESOURCE_ADDED' && isKnownContinuationForm(resourceType)) {
        formSubmissionPendingRef.current = true;
        if (formSubmissionResetTimeoutRef.current) {
          clearTimeout(formSubmissionResetTimeoutRef.current);
        }
        formSubmissionResetTimeoutRef.current = setTimeout(() => {
          formSubmissionPendingRef.current = false;
          formSubmissionResetTimeoutRef.current = null;
        }, 1000);

        if (!window.opener) {
          const submitStepId = SUBMIT_STEP_IDS[resourceType];
          const submitStepIndex = steps.findIndex(step => step.id === submitStepId);

          if (submitStepIndex !== -1) {
            const nextIndex = Math.min(submitStepIndex + 1, totalSteps - 1);
            const targetIndex = Math.max(currentStepIndexRef.current, nextIndex);
            const updatedCompletedSteps = new Set(completedStepsRef.current);
            updatedCompletedSteps.add(submitStepId);
            const completedStepsArray = Array.from(updatedCompletedSteps);

            currentStepIndexRef.current = targetIndex;
            completedStepsRef.current = updatedCompletedSteps;
            setCurrentStepIndex(targetIndex);
            void applyProgressUpdate({
              current_step: targetIndex,
              completed_steps: completedStepsArray,
            });
          }
        }
        return;
      }

      if (type === 'FORM_WINDOW_CLOSING' && isKnownContinuationForm(form)) {
        if (formSubmissionPendingRef.current) {
          return;
        }
        setContinuationForm(form);
      }
    };

    window.addEventListener('message', handleMessage);
    return () => {
      window.removeEventListener('message', handleMessage);
      if (formSubmissionResetTimeoutRef.current) {
        clearTimeout(formSubmissionResetTimeoutRef.current);
        formSubmissionResetTimeoutRef.current = null;
      }
    };
  }, []);

  const previousStep = useCallback(() => {
    setCurrentStepIndex(prev => {
      if (prev <= 0) {
        return 0;
      }
      const nextIndex = prev - 1;
      void applyProgressUpdate({ current_step: nextIndex });
      setIsActive(true);
      return nextIndex;
    });
  }, [applyProgressUpdate]);

  const nextStep = useCallback(() => {
    setCurrentStepIndex(prev => {
      if (prev >= totalSteps - 1) {
        if (!progress.completed) {
          setIsActive(false);
          const allStepIds = steps.map(step => step.id);
          setCompletedSteps(new Set(allStepIds));
          void applyProgressUpdate({
            current_step: totalSteps - 1,
            completed: true,
            completed_at: new Date().toISOString(),
            completed_steps: allStepIds,
          });
        }
        return totalSteps - 1;
      }
      const nextIndex = prev + 1;
      void applyProgressUpdate({ current_step: nextIndex });
      return nextIndex;
    });
  }, [applyProgressUpdate, progress.completed, steps, totalSteps]);

  const completeStep = useCallback(
    (stepId: string, autoAdvance: boolean = false) => {
      setCompletedSteps(prev => {
        if (prev.has(stepId)) {
          return prev;
        }
        const next = new Set(prev);
        next.add(stepId);
        const completedStepsArray = Array.from(next);
        const allCompleted = steps.every(step => next.has(step.id));
        
        // Don't include current_step if we're in a popup window (it will be stale)
        const updatePayload: Partial<TutorialProgress> = {
          completed_steps: completedStepsArray,
          ...(allCompleted
            ? { completed: true, completed_at: new Date().toISOString() }
            : {}),
        };
        
        // Only include current_step if we're in the main window
        if (!window.opener) {
          updatePayload.current_step = currentStepIndex;
        }
        
        void applyProgressUpdate(updatePayload);
        
        if (allCompleted) {
          setIsActive(false);
        }
        return next;
      });
      
      // Only auto-advance if explicitly requested (for button clicks that open/close windows)
      if (autoAdvance) {
        setTimeout(() => {
          nextStep();
        }, 100);
      }
    },
    [applyProgressUpdate, currentStepIndex, nextStep, steps]
  );

  const isStepCompleted = useCallback(
    (stepId: string) => completedSteps.has(stepId),
    [completedSteps]
  );

  useEffect(() => {
    if (!currentStep?.advanceOnMdnsSelection) {
      return;
    }
    const unregister = registerSelectionHandler(() => {
      completeStep(currentStep.id);
      nextStep();
    });
    return unregister;
  }, [completeStep, currentStep, nextStep, registerSelectionHandler]);

  const handleContinuation = useCallback(() => {
    setContinuationForm(prevForm => {
      if (!prevForm) {
        return null;
      }

      const target = CONTINUATION_TARGETS[prevForm];
      setIsActive(true);

      const stepIndex = steps.findIndex(step => step.id === target.stepId);
      if (stepIndex !== -1) {
        setCurrentStepIndex(stepIndex);
        void applyProgressUpdate({ current_step: stepIndex });
      }

      if (typeof window !== 'undefined') {
        const addPageType = prevForm === 'source' ? 'sources' : prevForm === 'sink' ? 'sinks' : 'routes';
        openAddPage(addPageType);
      }

      return null;
    });
  }, [applyProgressUpdate, steps]);

  const shouldDisplayStep = useMemo(() => {
    if (!currentStep) return false;
    if (!isActive) return false;
    if (isDesktopMode) return false;
    const routeHint = currentStep.routeHint;
    if (!routeHint) return true;
    const path = window.location.pathname;
    return path.startsWith(routeHint);
  }, [currentStep, isActive, isDesktopMode]);

  const contextValue = useMemo<TutorialContextValue>(() => ({
    steps,
    currentStepIndex,
    currentStep,
    isLoading,
    isActive,
    progress,
    completedSteps,
    startTutorial,
    restartTutorial,
    skipTutorial,
    skipToStep,
    nextStep,
    previousStep,
    completeStep,
    isStepCompleted,
  }), [
    steps,
    currentStepIndex,
    currentStep,
    isLoading,
    isActive,
    progress,
    completedSteps,
    startTutorial,
    restartTutorial,
    skipTutorial,
    skipToStep,
    nextStep,
    previousStep,
    completeStep,
    isStepCompleted,
  ]);

  const continuationLabel = continuationForm ? `${continuationForm} form` : 'form';
  if (!isActive && continuationForm)
    setContinuationForm(null);

  return (
    <TutorialContext.Provider value={contextValue}>
      {children}
      {!isDesktopMode && (
        <TutorialOverlay
          isLoading={isLoading}
          isActive={isActive}
          step={currentStep}
          stepIndex={currentStepIndex}
          totalSteps={totalSteps}
          onNext={nextStep}
          onPrevious={previousStep}
          onSkip={skipTutorial}
          onSkipToStep={skipToStep}
          onRestart={restartTutorial}
          shouldDisplay={shouldDisplayStep}
        />
      )}
      <AlertDialog
        isOpen={Boolean(continuationForm && isActive)}
        leastDestructiveRef={continuationCancelRef}
        onClose={() => setContinuationForm(null)}
        isCentered
      >
        <AlertDialogOverlay zIndex={10}>
          <AlertDialogContent>
            <AlertDialogHeader fontSize="lg" fontWeight="bold">
              Keep going with the tutorial?
            </AlertDialogHeader>
            <AlertDialogBody>
              {`You closed the add ${continuationLabel}. Reopen it to finish the current step, or skip the tutorial if you're done.`}
            </AlertDialogBody>
            <AlertDialogFooter>
              <Button ref={continuationCancelRef} onClick={handleContinuation}>
                I'll continue
              </Button>
              <Button
                colorScheme="red"
                ml={3}
                onClick={() => {
                  setContinuationForm(null);
                  skipTutorial();
                }}
              >
                Skip tutorial
              </Button>
            </AlertDialogFooter>
          </AlertDialogContent>
        </AlertDialogOverlay>
      </AlertDialog>
    </TutorialContext.Provider>
  );
};

export const useTutorial = (): TutorialContextValue => {
  const context = useContext(TutorialContext);
  if (!context) {
    throw new Error('useTutorial must be used within a TutorialProvider');
  }
  return context;
};
