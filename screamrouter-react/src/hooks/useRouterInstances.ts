import { useCallback, useEffect, useMemo, useRef, useState } from 'react';
import ApiService, { RouterMdnsService } from '../api/api';

export interface RouterInstance {
  id: string;
  label: string;
  hostname: string;
  address?: string;
  port: number;
  scheme: string;
  sitePath: string;
  origin: string;
  url: string;
  properties: Record<string, string>;
  uuid?: string;
  raw: RouterMdnsService;
  isCurrent: boolean;
}

interface UseRouterInstancesOptions {
  timeout?: number;
  pollInterval?: number;
}

const formatServiceName = (name?: string): string => {
  if (!name) {
    return '';
  }
  return name.replace(/\._screamrouter\.(_tcp)?\.local\.?$/i, '');
};

const sanitizeHost = (host?: string): string => {
  if (!host) {
    return '';
  }
  return host.replace(/\.$/, '');
};

const normalizePath = (path?: string): string => {
  if (!path) {
    return '/site';
  }
  if (path === '/') {
    return '/';
  }
  return path.startsWith('/') ? path : `/${path}`;
};

const buildInstances = (services: RouterMdnsService[]): RouterInstance[] => {
  const instances: RouterInstance[] = [];
  const currentOrigin = typeof window !== 'undefined' ? window.location.origin : '';
  const currentHost = typeof window !== 'undefined' ? window.location.hostname.toLowerCase() : '';
  const currentHostTrimmed = currentHost.replace(/\.local$/, '');

  services.forEach(service => {
    if (!service || typeof service.port !== 'number' || service.port <= 0) {
      return;
    }

    const properties = service.properties || {};
    const scheme = (properties.scheme || 'https').toLowerCase();
    const hostFromService = sanitizeHost(service.host);
    const hostFromProps = sanitizeHost(properties.hostname);
    const hostFromAddress = sanitizeHost(service.addresses?.[0]);
    const hostname = hostFromService || hostFromProps || hostFromAddress;

    if (!hostname) {
      return;
    }

    const parsedPort = Number.isFinite(service.port) ? service.port : Number(properties.port);
    const inferredPort = parsedPort && parsedPort > 0 ? parsedPort : (scheme === 'https' ? 443 : 80);
    const sitePath = normalizePath(properties.path || properties.site);
    const uuid = (properties.uuid || properties.UUID || '').trim();

    const origin = `${scheme}://${hostname}${((scheme === 'https' && inferredPort === 443) || (scheme === 'http' && inferredPort === 80)) ? '' : `:${inferredPort}`}`;
    const url = `${origin}${sitePath}`;

    const label = properties.hostname || formatServiceName(service.name) || hostname;

    const id = `${hostname}:${inferredPort}`;
    const address = service.addresses && service.addresses.length > 0 ? service.addresses[0] : undefined;

    const isCurrent = Boolean(
      origin === currentOrigin ||
      hostname.toLowerCase() === currentHost ||
      hostname.replace(/\.local$/, '').toLowerCase() === currentHostTrimmed ||
      address === currentHost
    );

    if (instances.some(instance => instance.id === id)) {
      return;
    }

    instances.push({
      id,
      label,
      hostname,
      address,
      port: inferredPort,
      scheme,
      sitePath,
      origin,
      url,
      properties,
      uuid: uuid || undefined,
      raw: service,
      isCurrent,
    });
  });

  return instances.sort((a, b) => {
    if (a.isCurrent && !b.isCurrent) {
      return -1;
    }
    if (!a.isCurrent && b.isCurrent) {
      return 1;
    }
    return a.label.localeCompare(b.label);
  });
};

export const useRouterInstances = (
  options: UseRouterInstancesOptions = {}
) => {
  const { timeout = 1.5, pollInterval = 20000 } = options;
  const [instances, setInstances] = useState<RouterInstance[]>([]);
  const [loading, setLoading] = useState<boolean>(true);
  const [error, setError] = useState<string | null>(null);
  const isMountedRef = useRef(true);

  useEffect(() => {
    isMountedRef.current = true;
    return () => {
      isMountedRef.current = false;
    };
  }, []);

  const fetchInstances = useCallback(async () => {
    setLoading(true);
    try {
      const response = await ApiService.getRouterServices(timeout);
      if (!isMountedRef.current) {
        return;
      }
      const formatted = buildInstances(response.data.services || []);
      setInstances(formatted);
      setError(null);
    } catch (err) {
      console.error('Failed to fetch router services', err);
      if (isMountedRef.current) {
        setError('Unable to discover other instances right now.');
      }
    } finally {
      if (isMountedRef.current) {
        setLoading(false);
      }
    }
  }, [timeout]);

  useEffect(() => {
    let intervalId: number | undefined;

    const load = () => {
      void fetchInstances();
    };

    load();

    if (pollInterval > 0) {
      intervalId = window.setInterval(load, pollInterval);
    }

    return () => {
      if (intervalId) {
        window.clearInterval(intervalId);
      }
    };
  }, [fetchInstances, pollInterval]);

  const state = useMemo(() => ({
    instances,
    loading,
    error,
  }), [instances, loading, error]);

  return {
    ...state,
    refresh: fetchInstances,
  };
};
