import FFT from 'fft.js';
import { TimeshiftRecording } from '../api/timeshift';

export interface SpectrogramData {
  timeBins: number;
  freqBins: number;
  data: Float32Array;
  maxMagnitude: number;
  minMagnitude: number;
  fftSize: number;
  hopSize: number;
}

export interface TimeshiftAnalysis {
  recording: TimeshiftRecording;
  monoSamples: Float32Array;
  totalSamples: number;
  bytesPerFrame: number;
  spectrogram: SpectrogramData;
}

const clamp = (value: number, min: number, max: number): number => Math.min(Math.max(value, min), max);

const decodeSample = (view: DataView, byteOffset: number, bitDepth: number): number => {
  switch (bitDepth) {
    case 8: {
      // Unsigned PCM
      const value = view.getUint8(byteOffset);
      return (value - 128) / 128;
    }
    case 16:
      return view.getInt16(byteOffset, true) / 32768;
    case 24: {
      const b0 = view.getUint8(byteOffset);
      const b1 = view.getUint8(byteOffset + 1);
      const b2 = view.getUint8(byteOffset + 2);
      let value = (b2 << 16) | (b1 << 8) | b0;
      if (value & 0x800000) {
        value |= 0xFF000000;
      }
      return value / 0x800000;
    }
    case 32:
      return view.getInt32(byteOffset, true) / 2147483648;
    default:
      return 0;
  }
};

export const convertRecordingToAnalysis = (recording: TimeshiftRecording): TimeshiftAnalysis => {
  const { pcm, channels, bitDepth, sampleRate } = recording;
  const bytesPerSample = bitDepth / 8;
  if (!Number.isInteger(bytesPerSample) || bytesPerSample <= 0 || channels <= 0 || sampleRate <= 0) {
    throw new Error('Invalid PCM metadata');
  }
  const frameSize = channels * bytesPerSample;
  if (frameSize === 0 || pcm.byteLength % frameSize !== 0) {
    throw new Error('PCM data length does not align with metadata');
  }

  const totalSamples = pcm.byteLength / frameSize;
  const monoSamples = new Float32Array(totalSamples);
  const view = new DataView(pcm);

  for (let frame = 0; frame < totalSamples; frame += 1) {
    let accum = 0;
    for (let ch = 0; ch < channels; ch += 1) {
      const byteOffset = (frame * channels + ch) * bytesPerSample;
      accum += decodeSample(view, byteOffset, bitDepth);
    }
    monoSamples[frame] = accum / channels;
  }

  const spectrogram = computeSpectrogram(monoSamples, sampleRate);

  return {
    recording,
    monoSamples,
    totalSamples,
    bytesPerFrame: frameSize,
    spectrogram,
  };
};

export const computeWaveformBuckets = (
  samples: Float32Array,
  sampleRate: number,
  startTime: number,
  endTime: number,
  bucketCount: number,
): { mins: Float32Array; maxs: Float32Array } => {
  const clampedStart = clamp(startTime, 0, endTime);
  const clampedEnd = clamp(endTime, clampedStart, samples.length / sampleRate);
  const startSample = Math.floor(clampedStart * sampleRate);
  const endSample = Math.max(startSample + 1, Math.min(samples.length, Math.ceil(clampedEnd * sampleRate)));
  const sliceLength = endSample - startSample;
  const mins = new Float32Array(bucketCount);
  const maxs = new Float32Array(bucketCount);

  if (sliceLength <= 0) {
    return { mins, maxs };
  }

  const samplesPerBucket = Math.max(1, Math.floor(sliceLength / bucketCount));

  for (let bucket = 0; bucket < bucketCount; bucket += 1) {
    let bucketMin = 1;
    let bucketMax = -1;

    const bucketStart = startSample + bucket * samplesPerBucket;
    const bucketEnd = bucket === bucketCount - 1
      ? endSample
      : Math.min(endSample, bucketStart + samplesPerBucket);

    for (let idx = bucketStart; idx < bucketEnd; idx += 1) {
      const value = samples[idx];
      if (value < bucketMin) bucketMin = value;
      if (value > bucketMax) bucketMax = value;
    }

    mins[bucket] = bucketMin;
    maxs[bucket] = bucketMax;
  }

  return { mins, maxs };
};

export const computeSpectrogram = (
  samples: Float32Array,
  sampleRate: number,
  fftSize = 1024,
  hopSize = 512,
): SpectrogramData => {
  const fft = new FFT(fftSize);
  const complexSpectrum = fft.createComplexArray();
  const window = new Float32Array(fftSize);
  for (let i = 0; i < fftSize; i += 1) {
    window[i] = 0.5 * (1 - Math.cos((2 * Math.PI * i) / (fftSize - 1)));
  }

  const timeBins = Math.max(1, Math.floor((samples.length - fftSize) / hopSize) + 1);
  const freqBins = fftSize / 2;
  const data = new Float32Array(timeBins * freqBins);

  let globalMax = Number.NEGATIVE_INFINITY;
  let globalMin = Number.POSITIVE_INFINITY;

  const input = new Float32Array(fftSize);

  for (let frame = 0; frame < timeBins; frame += 1) {
    const start = frame * hopSize;
    for (let i = 0; i < fftSize; i += 1) {
      input[i] = samples[start + i] * window[i];
    }

    fft.realTransform(complexSpectrum, input);
    fft.completeSpectrum(complexSpectrum);

    for (let bin = 0; bin < freqBins; bin += 1) {
      const real = complexSpectrum[2 * bin];
      const imag = complexSpectrum[2 * bin + 1];
      const magnitude = Math.sqrt(real * real + imag * imag);
      // Logarithmic compression for better dynamic range visualization
      const value = Math.log10(1 + magnitude);
      data[frame * freqBins + bin] = value;
      if (value > globalMax) globalMax = value;
      if (value < globalMin) globalMin = value;
    }
  }

  if (!Number.isFinite(globalMax) || globalMax <= globalMin) {
    globalMax = 1;
    globalMin = 0;
  }

  return {
    timeBins,
    freqBins,
    data,
    maxMagnitude: globalMax,
    minMagnitude: globalMin,
    fftSize,
    hopSize,
  };
};

export const getSpectrogramIntensity = (
  spectrogram: SpectrogramData,
  timeBin: number,
  freqBin: number,
): number => {
  const { timeBins, freqBins, data, maxMagnitude, minMagnitude } = spectrogram;
  const t = clamp(timeBin, 0, timeBins - 1);
  const f = clamp(freqBin, 0, freqBins - 1);
  const idx = t * freqBins + f;
  const value = data[idx];
  return (value - minMagnitude) / (maxMagnitude - minMagnitude + Number.EPSILON);
};

export const extractPCMWindow = (
  analysis: TimeshiftAnalysis,
  startSeconds: number,
  endSeconds: number,
): ArrayBuffer => {
  const { recording, bytesPerFrame } = analysis;
  const { sampleRate, pcm } = recording;
  const totalFrames = pcm.byteLength / bytesPerFrame;
  const clampedStart = clamp(startSeconds, 0, recording.durationSeconds);
  const clampedEnd = clamp(endSeconds, clampedStart, recording.durationSeconds);

  const startFrame = Math.floor(clampedStart * sampleRate);
  const endFrame = Math.min(totalFrames, Math.ceil(clampedEnd * sampleRate));

  const startByte = startFrame * bytesPerFrame;
  const endByte = endFrame * bytesPerFrame;

  return pcm.slice(startByte, endByte);
};

export const buildWavFile = (
  pcmData: ArrayBuffer,
  sampleRate: number,
  channels: number,
  bitDepth: number,
): ArrayBuffer => {
  const headerSize = 44;
  const totalSize = headerSize + pcmData.byteLength;
  const buffer = new ArrayBuffer(totalSize);
  const view = new DataView(buffer);
  const bytes = new Uint8Array(buffer);
  const pcmBytes = new Uint8Array(pcmData);

  const writeString = (offset: number, value: string) => {
    for (let i = 0; i < value.length; i += 1) {
      view.setUint8(offset + i, value.charCodeAt(i));
    }
  };

  const blockAlign = channels * (bitDepth / 8);
  const byteRate = sampleRate * blockAlign;

  writeString(0, 'RIFF');
  view.setUint32(4, totalSize - 8, true);
  writeString(8, 'WAVE');
  writeString(12, 'fmt ');
  view.setUint32(16, 16, true); // PCM subchunk size
  view.setUint16(20, 1, true); // PCM format
  view.setUint16(22, channels, true);
  view.setUint32(24, sampleRate, true);
  view.setUint32(28, byteRate, true);
  view.setUint16(32, blockAlign, true);
  view.setUint16(34, bitDepth, true);
  writeString(36, 'data');
  view.setUint32(40, pcmData.byteLength, true);

  bytes.set(pcmBytes, headerSize);

  return buffer;
};

export const buildWavBlob = (
  pcmData: ArrayBuffer,
  sampleRate: number,
  channels: number,
  bitDepth: number,
): Blob => {
  const wavBuffer = buildWavFile(pcmData, sampleRate, channels, bitDepth);
  return new Blob([wavBuffer], { type: 'audio/wav' });
};
