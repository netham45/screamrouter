import { DiscoveredDevice } from '../types/preferences';

const methodColorMap: Record<string, string> = {
  mdns: 'purple',
  cpp_rtp: 'green',
  cpp_raw: 'teal',
  per_process: 'orange',
  pulse: 'pink',
  cpp_sap: 'yellow',
};

export const getMethodColor = (method: string): string => {
  const normalized = method?.toLowerCase?.() ?? '';
  return methodColorMap[normalized] ?? 'gray';
};

export const buildDeviceKey = (device: DiscoveredDevice): string => {
  const identifierValue = device.properties['identifier'];
  const identifier = typeof identifierValue === 'string' && identifierValue
    ? identifierValue
    : device.tag ?? device.ip;
  return `${device.discovery_method}:${identifier}`;
};

export const formatLastSeen = (value: string | null | undefined) => {
  if (!value) return 'Recently discovered';
  try {
    const date = new Date(value);
    if (Number.isNaN(date.getTime())) {
      return 'Recently discovered';
    }
    return `Last seen ${date.toLocaleString()}`;
  } catch (error) {
    return 'Recently discovered';
  }
};

const MAX_DEVICE_AGE_SECONDS = 900;

export const isDeviceRecentlySeen = (
  device: DiscoveredDevice,
  maxAgeSeconds: number = MAX_DEVICE_AGE_SECONDS
) => {
  if (!device.last_seen) {
    return true;
  }
  const timestamp = Date.parse(device.last_seen);
  if (Number.isNaN(timestamp)) {
    return false;
  }
  const ageSeconds = (Date.now() - timestamp) / 1000;
  return ageSeconds <= maxAgeSeconds;
};
