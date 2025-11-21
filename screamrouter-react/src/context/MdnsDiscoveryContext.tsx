import React, {
  createContext,
  useCallback,
  useContext,
  useEffect,
  useMemo,
  useRef,
  useState,
} from 'react';
import ApiService from '../api/api';
import MdnsDiscoveryModal from '../components/tutorial/MdnsDiscoveryModal';
import { DiscoveredDevice } from '../types/preferences';
import { buildDeviceKey } from '../utils/discovery';

type MdnsFilter = 'all' | 'sources' | 'sinks';
type MdnsSelectionHandler = (device: DiscoveredDevice) => void;

interface MdnsDiscoveryContextValue {
  isModalOpen: boolean;
  isLoading: boolean;
  devices: DiscoveredDevice[];
  error: string | null;
  filter: MdnsFilter;
  openModal: (filter?: MdnsFilter) => void;
  closeModal: () => void;
  refreshDevices: () => Promise<void>;
  registerSelectionHandler: (handler: MdnsSelectionHandler) => () => void;
}

const MdnsDiscoveryContext = createContext<MdnsDiscoveryContextValue | undefined>(undefined);

export const MdnsDiscoveryProvider: React.FC<{ children: React.ReactNode }> = ({ children }) => {
  const [isModalOpen, setIsModalOpen] = useState(false);
  const [isLoading, setIsLoading] = useState(false);
  const [devices, setDevices] = useState<DiscoveredDevice[]>([]);
  const [error, setError] = useState<string | null>(null);
  const [filter, setFilter] = useState<MdnsFilter>('all');

  const handlersRef = useRef<Set<MdnsSelectionHandler>>(new Set());
  const isMountedRef = useRef(true);

  useEffect(() => {
    // React 18 StrictMode runs effects twice in development, so always re-flag as mounted.
    isMountedRef.current = true;
    return () => {
      isMountedRef.current = false;
    };
  }, []);

  const refreshDevices = useCallback(async () => {
    setIsLoading(true);
    setError(null);
    try {
      const response = await ApiService.getDiscoverySnapshot();
      if (!isMountedRef.current) return;
      setDevices(response.data?.discovered_devices ?? []);
    } catch (err) {
      if (!isMountedRef.current) return;
      console.error('Unable to fetch mDNS devices', err);
      setError('Unable to retrieve devices. Please try again.');
    } finally {
      if (isMountedRef.current) {
        setIsLoading(false);
      }
    }
  }, []);

  const openModal = useCallback(
    (nextFilter: MdnsFilter = 'all') => {
      setFilter(nextFilter);
      setIsModalOpen(true);
      void refreshDevices();
    },
    [refreshDevices]
  );

  const closeModal = useCallback(() => {
    setIsModalOpen(false);
  }, []);

  const registerSelectionHandler = useCallback((handler: MdnsSelectionHandler) => {
    handlersRef.current.add(handler);
    return () => {
      handlersRef.current.delete(handler);
    };
  }, []);

  const handleSelect = useCallback((device: DiscoveredDevice) => {
    const deviceKey = buildDeviceKey(device);

    setDevices(prev => prev.filter(entry => {
      return buildDeviceKey(entry) !== deviceKey;
    }));

    const handlers = Array.from(handlersRef.current);
    handlers.forEach(handler => {
      try {
        handler(device);
      } catch (error) {
        console.error('Error handling mDNS selection', error);
      }
    });

    setIsModalOpen(false);
  }, []);

  const handleRefreshClick = useCallback(() => {
    void refreshDevices();
  }, [refreshDevices]);

  const contextValue = useMemo<MdnsDiscoveryContextValue>(
    () => ({
      isModalOpen,
      isLoading,
      devices,
      error,
      filter,
      openModal,
      closeModal,
      refreshDevices,
      registerSelectionHandler,
    }),
    [
      isModalOpen,
      isLoading,
      devices,
      error,
      filter,
      openModal,
      closeModal,
      refreshDevices,
      registerSelectionHandler,
    ]
  );

  return (
    <MdnsDiscoveryContext.Provider value={contextValue}>
      {children}
      <MdnsDiscoveryModal
        isOpen={isModalOpen}
        isLoading={isLoading}
        devices={devices}
        error={error}
        filter={filter}
        onClose={closeModal}
        onRefresh={handleRefreshClick}
        onSelect={handleSelect}
      />
    </MdnsDiscoveryContext.Provider>
  );
};

export const useMdnsDiscovery = (): MdnsDiscoveryContextValue => {
  const context = useContext(MdnsDiscoveryContext);
  if (!context) {
    throw new Error('useMdnsDiscovery must be used within a MdnsDiscoveryProvider');
  }
  return context;
};
