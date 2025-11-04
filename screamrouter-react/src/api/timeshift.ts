import axios from 'axios';

export interface TimeshiftRecording {
  pcm: ArrayBuffer;
  sampleRate: number;
  channels: number;
  bitDepth: number;
  durationSeconds: number;
  earliestPacketAgeSeconds: number;
  latestPacketAgeSeconds: number;
  lookbackSeconds: number;
}

const parseHeaderNumber = (headers: Record<string, string>, key: string, fallback = 0): number => {
  const raw = headers[key];
  if (raw === undefined) {
    return fallback;
  }
  const value = Number(raw);
  return Number.isFinite(value) ? value : fallback;
};

export const fetchTimeshiftRecording = async (
  sourceTag: string,
  lookbackSeconds = 300,
): Promise<TimeshiftRecording> => {
  const response = await axios.get<ArrayBuffer>(`/api/timeshift/${encodeURIComponent(sourceTag)}`, {
    params: { lookback_seconds: lookbackSeconds },
    responseType: 'arraybuffer',
  });

  // Axios lower-cases header keys
  const headers = response.headers as Record<string, string>;

  return {
    pcm: response.data,
    sampleRate: parseHeaderNumber(headers, 'x-audio-sample-rate'),
    channels: parseHeaderNumber(headers, 'x-audio-channels'),
    bitDepth: parseHeaderNumber(headers, 'x-audio-bit-depth'),
    durationSeconds: parseHeaderNumber(headers, 'x-audio-duration-seconds'),
    earliestPacketAgeSeconds: parseHeaderNumber(headers, 'x-audio-earliest-packet-age-seconds'),
    latestPacketAgeSeconds: parseHeaderNumber(headers, 'x-audio-latest-packet-age-seconds'),
    lookbackSeconds: parseHeaderNumber(headers, 'x-audio-lookback-seconds', lookbackSeconds),
  };
};

export default {
  fetchTimeshiftRecording,
};
