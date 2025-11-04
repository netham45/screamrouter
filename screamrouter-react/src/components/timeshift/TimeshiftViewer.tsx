import React, { useCallback, useEffect, useMemo, useRef, useState } from 'react';
import { Box, useColorModeValue } from '@chakra-ui/react';
import { TimeshiftAnalysis, computeWaveformBuckets, getSpectrogramIntensity } from '../../utils/timeshiftProcessing';

export interface TimeRange {
  start: number;
  end: number;
}

interface TimeshiftViewerProps {
  analysis: TimeshiftAnalysis;
  viewRange: TimeRange;
  selection: TimeRange | null;
  onSelectionChange: (range: TimeRange | null) => void;
  onZoom?: (focusTime: number, deltaY: number) => void;
  onPan?: (deltaSeconds: number) => void;
}

const MIN_SELECTION_SECONDS = 0.01;

const colorFromIntensity = (value: number): [number, number, number] => {
  const clamped = Math.max(0, Math.min(1, value));
  const r = Math.round(255 * Math.pow(clamped, 0.6));
  const g = Math.round(255 * Math.pow(clamped, 1.2) * 0.8);
  const b = Math.round(255 * (1 - clamped));
  return [r, g, b];
};

const TimeshiftViewer: React.FC<TimeshiftViewerProps> = ({
  analysis,
  viewRange,
  selection,
  onSelectionChange,
  onZoom,
  onPan,
}) => {
  const containerRef = useRef<HTMLDivElement>(null);
  const waveformCanvasRef = useRef<HTMLCanvasElement>(null);
  const spectrogramCanvasRef = useRef<HTMLCanvasElement>(null);
  const [canvasWidth, setCanvasWidth] = useState(0);
  const [dragAnchor, setDragAnchor] = useState<number | null>(null);
  const [isSelecting, setIsSelecting] = useState(false);
  const [isPanning, setIsPanning] = useState(false);
  const panLastXRef = useRef<number>(0);

  const highlightColor = useColorModeValue('rgba(0, 123, 255, 0.15)', 'rgba(0, 198, 255, 0.25)');
  const axisColor = useColorModeValue('#2d3748', '#e2e8f0');
  const waveformBg = useColorModeValue('#edf2f7', '#2d3748');
  const waveformStroke = useColorModeValue('#1a202c', '#f7fafc');

  const { monoSamples, recording, spectrogram } = analysis;
  const duration = recording.durationSeconds;

  const resizeObserver = useMemo(
    () =>
      new ResizeObserver((entries) => {
        for (const entry of entries) {
          if (entry.contentRect.width) {
            setCanvasWidth(Math.floor(entry.contentRect.width));
          }
        }
      }),
    [],
  );

  useEffect(() => {
    const container = containerRef.current;
    if (!container) return;
    resizeObserver.observe(container);
    return () => resizeObserver.disconnect();
  }, [resizeObserver]);

  const drawWaveform = useCallback(() => {
    const canvas = waveformCanvasRef.current;
    if (!canvas || canvasWidth === 0) {
      return;
    }
    const ctx = canvas.getContext('2d');
    if (!ctx) return;

    const devicePixelRatio = window.devicePixelRatio || 1;
    const height = canvas.clientHeight || 160;
    canvas.width = canvasWidth * devicePixelRatio;
    canvas.height = height * devicePixelRatio;
    ctx.scale(devicePixelRatio, devicePixelRatio);

    ctx.clearRect(0, 0, canvasWidth, height);
    ctx.fillStyle = waveformBg;
    ctx.fillRect(0, 0, canvasWidth, height);

    const bucketData = computeWaveformBuckets(
      monoSamples,
      recording.sampleRate,
      viewRange.start,
      viewRange.end,
      canvasWidth,
    );

    ctx.strokeStyle = waveformStroke;
    ctx.beginPath();
    for (let x = 0; x < canvasWidth; x += 1) {
      const min = bucketData.mins[x] ?? 0;
      const max = bucketData.maxs[x] ?? 0;
      const yMin = ((1 - min) * height) / 2;
      const yMax = ((1 - max) * height) / 2;
      ctx.moveTo(x, yMin);
      ctx.lineTo(x, yMax);
    }
    ctx.stroke();

    // Draw zero axis
    ctx.strokeStyle = axisColor;
    ctx.beginPath();
    ctx.moveTo(0, height / 2);
    ctx.lineTo(canvasWidth, height / 2);
    ctx.stroke();

    if (selection) {
      const selStartRatio = (selection.start - viewRange.start) / (viewRange.end - viewRange.start);
      const selEndRatio = (selection.end - viewRange.start) / (viewRange.end - viewRange.start);
      const startX = Math.max(0, Math.floor(selStartRatio * canvasWidth));
      const endX = Math.min(canvasWidth, Math.ceil(selEndRatio * canvasWidth));
      if (endX > startX) {
        ctx.fillStyle = highlightColor;
        ctx.fillRect(startX, 0, endX - startX, height);
      }
    }
  }, [
    monoSamples,
    recording.sampleRate,
    viewRange.start,
    viewRange.end,
    canvasWidth,
    axisColor,
    selection,
    highlightColor,
    waveformBg,
    waveformStroke,
  ]);

  const drawSpectrogram = useCallback(() => {
    const canvas = spectrogramCanvasRef.current;
    if (!canvas || canvasWidth === 0) {
      return;
    }
    const ctx = canvas.getContext('2d');
    if (!ctx) return;

    const devicePixelRatio = window.devicePixelRatio || 1;
    const height = canvas.clientHeight || 240;
    canvas.width = canvasWidth * devicePixelRatio;
    canvas.height = height * devicePixelRatio;
    ctx.scale(devicePixelRatio, devicePixelRatio);

    ctx.clearRect(0, 0, canvasWidth, height);

    if (duration <= 0) {
      return;
    }

    const startBin = Math.max(
      0,
      Math.floor((viewRange.start / duration) * spectrogram.timeBins),
    );
    const endBin = Math.max(
      startBin + 1,
      Math.ceil((viewRange.end / duration) * spectrogram.timeBins),
    );

    const timeBinsVisible = endBin - startBin;
    const freqBins = spectrogram.freqBins;
    const imageData = ctx.createImageData(canvasWidth, height);
    const data = imageData.data;

    for (let x = 0; x < canvasWidth; x += 1) {
      const timeRatio = x / canvasWidth;
      const timeBin = clampBin(startBin + Math.floor(timeRatio * timeBinsVisible), spectrogram.timeBins);
      for (let y = 0; y < height; y += 1) {
        const freqRatio = 1 - y / height;
        const freqBin = clampBin(Math.floor(freqRatio * freqBins), freqBins);
        const intensity = getSpectrogramIntensity(spectrogram, timeBin, freqBin);
        const [r, g, b] = colorFromIntensity(intensity);
        const idx = (y * canvasWidth + x) * 4;
        data[idx] = r;
        data[idx + 1] = g;
        data[idx + 2] = b;
        data[idx + 3] = 255;
      }
    }

    ctx.putImageData(imageData, 0, 0);

    if (selection) {
      const selStartRatio = (selection.start - viewRange.start) / (viewRange.end - viewRange.start);
      const selEndRatio = (selection.end - viewRange.start) / (viewRange.end - viewRange.start);
      const startX = Math.max(0, Math.floor(selStartRatio * canvasWidth));
      const endX = Math.min(canvasWidth, Math.ceil(selEndRatio * canvasWidth));
      if (endX > startX) {
        ctx.fillStyle = highlightColor;
        ctx.fillRect(startX, 0, endX - startX, height);
      }
    }
  }, [spectrogram, canvasWidth, duration, viewRange.start, viewRange.end, selection, highlightColor]);

  useEffect(() => {
    drawWaveform();
  }, [drawWaveform]);

  useEffect(() => {
    drawSpectrogram();
  }, [drawSpectrogram]);

  const handlePointerDown = (event: React.PointerEvent<HTMLDivElement>) => {
    if (event.cancelable) {
      event.preventDefault();
    }
    event.stopPropagation();
    event.stopPropagation();
    if (!containerRef.current) return;
    if (event.button === 1) {
      setIsPanning(true);
      panLastXRef.current = event.clientX;
      (event.target as HTMLElement).setPointerCapture(event.pointerId);
      return;
    }
    if (event.button !== 0) {
      return;
    }
    const rect = containerRef.current.getBoundingClientRect();
    const relativeX = (event.clientX - rect.left) / rect.width;
    const time = viewRange.start + relativeX * (viewRange.end - viewRange.start);
    setDragAnchor(time);
    setIsSelecting(true);
    onSelectionChange({ start: time, end: time });
    (event.target as HTMLElement).setPointerCapture(event.pointerId);
  };

  const handlePointerMove = (event: React.PointerEvent<HTMLDivElement>) => {
    if (!containerRef.current) return;
    if (isPanning) {
      event.preventDefault();
      const dx = event.clientX - panLastXRef.current;
      panLastXRef.current = event.clientX;
      const rect = containerRef.current.getBoundingClientRect();
      const rangeDuration = viewRange.end - viewRange.start;
      if (rect.width > 0 && rangeDuration > 0 && onPan) {
        const deltaSeconds = -(dx / rect.width) * rangeDuration;
        onPan(deltaSeconds);
      }
      return;
    }
    if (!isSelecting || dragAnchor === null) return;
    event.preventDefault();
    event.stopPropagation();
    const rect = containerRef.current.getBoundingClientRect();
    const relativeX = (event.clientX - rect.left) / rect.width;
    const time = viewRange.start + clamp(relativeX, 0, 1) * (viewRange.end - viewRange.start);
    const start = Math.min(dragAnchor, time);
    const end = Math.max(dragAnchor, time);
    if (end - start >= MIN_SELECTION_SECONDS) {
      onSelectionChange({ start, end });
    } else {
      onSelectionChange({ start: dragAnchor, end: dragAnchor });
    }
  };

  const handlePointerUp = (event: React.PointerEvent<HTMLDivElement>) => {
    const isMiddleButton = event.button === 1 || event.type === 'pointercancel';
    if (isMiddleButton && isPanning) {
      setIsPanning(false);
      (event.target as HTMLElement).releasePointerCapture(event.pointerId);
      return;
    }
    if (!isSelecting || dragAnchor === null) return;
    event.preventDefault();
    if (selection && Math.abs(selection.end - selection.start) < MIN_SELECTION_SECONDS) {
      onSelectionChange(null);
    } else if (selection) {
      onSelectionChange({
        start: Math.max(viewRange.start, Math.min(selection.start, selection.end)),
        end: Math.min(viewRange.end, Math.max(selection.start, selection.end)),
      });
    }
    setDragAnchor(null);
    setIsSelecting(false);
    (event.target as HTMLElement).releasePointerCapture(event.pointerId);
  };

  const handleDoubleClick = () => {
    onSelectionChange(null);
  };

  const handleNativeWheel = useCallback((event: WheelEvent) => {
    if (!containerRef.current || !onZoom) {
      return;
    }
    event.preventDefault();
    const rect = containerRef.current.getBoundingClientRect();
    const ratio = clamp((event.clientX - rect.left) / rect.width, 0, 1);
    const focusTime = viewRange.start + ratio * (viewRange.end - viewRange.start);
    onZoom(focusTime, event.deltaY);
  }, [onZoom, viewRange.start, viewRange.end]);

  useEffect(() => {
    const element = containerRef.current;
    if (!element || !handleNativeWheel) {
      return;
    }
    const listener = (event: WheelEvent) => handleNativeWheel(event);
    element.addEventListener('wheel', listener, { passive: false });
    return () => {
      element.removeEventListener('wheel', listener);
    };
  }, [handleNativeWheel]);

  return (
    <Box
      ref={containerRef}
      position="relative"
      width="100%"
      maxWidth="100%"
      userSelect="none"
      onPointerDown={handlePointerDown}
      onPointerMove={handlePointerMove}
      onPointerUp={handlePointerUp}
      onPointerCancel={handlePointerUp}
      onDoubleClick={handleDoubleClick}
    >
      <canvas
        ref={waveformCanvasRef}
        style={{ width: '100%', maxWidth: '100%', height: '160px', display: 'block' }}
      />
      <canvas
        ref={spectrogramCanvasRef}
        style={{ width: '100%', maxWidth: '100%', height: '240px', display: 'block' }}
      />
    </Box>
  );
};

export default TimeshiftViewer;

const clamp = (value: number, min: number, max: number): number => Math.min(Math.max(value, min), max);

const clampBin = (value: number, maxExclusive: number): number => {
  if (value < 0) return 0;
  if (value >= maxExclusive) return maxExclusive - 1;
  return value;
};
