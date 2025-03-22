import React, { useRef, useEffect, useState } from 'react';
import { Box, Flex, Text } from '@chakra-ui/react';

interface MusicVisualizerProps {
  isPlaying: boolean;
  audioData?: number[]; // Optional audio frequency data
  theme?: 'vibrant' | 'dynamic' | 'calm'; // Different visualization themes
  height?: string; // Container height
}

/**
 * A dynamic music visualizer component that provides a visual representation
 * of audio being played. The visualizer animates according to audio data
 * or creates a simulation when no data is provided.
 */
const MusicVisualizer: React.FC<MusicVisualizerProps> = ({
  isPlaying = false,
  audioData,
  theme = 'vibrant',
  height = '120px'
}) => {
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const [dimensions, setDimensions] = useState({ width: 0, height: 0 });
  const animationRef = useRef<number>();
  const simulatedBars = 64; // Number of bars in simulation mode
  
  // Simulated data generation for when no audio data is provided
  const generateSimulatedData = () => {
    const data = [];
    for (let i = 0; i < simulatedBars; i++) {
      // Create a wave-like pattern with randomness
      const baseHeight = Math.sin(i * 0.2 + Date.now() * 0.001) * 0.5 + 0.5;
      const randomFactor = isPlaying ? 0.3 : 0.05;
      const height = baseHeight * (1 - randomFactor) + Math.random() * randomFactor;
      data.push(height * 0.8); // Scale down a bit
    }
    return data;
  };

  // Get theme colors
  const getThemeColors = () => {
    switch (theme) {
      case 'vibrant':
        return {
          primary: '#9B59B6',
          secondary: '#3498DB',
          accent: '#E91E63',
          background: 'rgba(155, 89, 182, 0.05)'
        };
      case 'dynamic':
        return {
          primary: '#FF5722',
          secondary: '#F1C40F',
          accent: '#9B59B6',
          background: 'rgba(255, 87, 34, 0.05)'
        };
      case 'calm':
        return {
          primary: '#1ABC9C',
          secondary: '#2ECC71',
          accent: '#3498DB',
          background: 'rgba(26, 188, 156, 0.05)'
        };
      default:
        return {
          primary: '#9B59B6',
          secondary: '#3498DB',
          accent: '#E91E63',
          background: 'rgba(155, 89, 182, 0.05)'
        };
    }
  };

  // Set up canvas dimensions on mount and resize
  useEffect(() => {
    const updateSize = () => {
      if (canvasRef.current) {
        const canvas = canvasRef.current;
        const container = canvas.parentElement;
        if (container) {
          const { width } = container.getBoundingClientRect();
          // Height is set by the height prop
          const heightValue = parseInt(height, 10);
          
          // Set canvas dimensions with device pixel ratio for sharpness
          const dpr = window.devicePixelRatio || 1;
          canvas.width = width * dpr;
          canvas.height = heightValue * dpr;
          
          // Set canvas CSS dimensions
          canvas.style.width = `${width}px`;
          canvas.style.height = `${heightValue}px`;
          
          setDimensions({ width, height: heightValue });
        }
      }
    };

    window.addEventListener('resize', updateSize);
    updateSize();
    
    return () => window.removeEventListener('resize', updateSize);
  }, [height]);

  // Animation loop
  useEffect(() => {
    if (!canvasRef.current) return;
    
    const canvas = canvasRef.current;
    const ctx = canvas.getContext('2d');
    if (!ctx) return;
    
    const colors = getThemeColors();
    
    const draw = () => {
      // Scale context for device pixel ratio
      const dpr = window.devicePixelRatio || 1;
      ctx.scale(dpr, dpr);
      
      // Clear canvas for the next frame
      ctx.clearRect(0, 0, dimensions.width, dimensions.height);
      
      // Use provided data or generate simulated data
      const data = audioData || generateSimulatedData();
      
      // Calculate bar width based on canvas width and number of data points
      const barWidth = dimensions.width / data.length;
      const spacing = 2; // Space between bars
      const adjustedBarWidth = barWidth - spacing;
      
      // Create gradient background
      const bgGradient = ctx.createLinearGradient(0, 0, dimensions.width, 0);
      bgGradient.addColorStop(0, colors.background);
      bgGradient.addColorStop(0.5, 'rgba(255, 255, 255, 0.02)');
      bgGradient.addColorStop(1, colors.background);
      
      ctx.fillStyle = bgGradient;
      ctx.fillRect(0, 0, dimensions.width, dimensions.height);
      
      // Draw each bar
      data.forEach((value, i) => {
        const barHeight = value * dimensions.height;
        const x = i * barWidth;
        
        // Create vertical gradient for each bar
        const gradient = ctx.createLinearGradient(
          0, dimensions.height - barHeight, 
          0, dimensions.height
        );
        
        if (theme === 'vibrant') {
          gradient.addColorStop(0, colors.accent);
          gradient.addColorStop(0.5, colors.primary);
          gradient.addColorStop(1, colors.secondary);
        } else if (theme === 'dynamic') {
          gradient.addColorStop(0, colors.primary);
          gradient.addColorStop(0.7, colors.secondary);
          gradient.addColorStop(1, colors.accent);
        } else {
          gradient.addColorStop(0, colors.secondary);
          gradient.addColorStop(0.6, colors.primary);
          gradient.addColorStop(1, colors.accent);
        }
        
        ctx.fillStyle = gradient;
        
        // Draw bar with rounded top
        const cornerRadius = 2;
        const barX = x + spacing / 2;
        const barY = dimensions.height - barHeight;
        
        ctx.beginPath();
        ctx.moveTo(barX + cornerRadius, barY);
        ctx.lineTo(barX + adjustedBarWidth - cornerRadius, barY);
        ctx.quadraticCurveTo(barX + adjustedBarWidth, barY, barX + adjustedBarWidth, barY + cornerRadius);
        ctx.lineTo(barX + adjustedBarWidth, dimensions.height);
        ctx.lineTo(barX, dimensions.height);
        ctx.lineTo(barX, barY + cornerRadius);
        ctx.quadraticCurveTo(barX, barY, barX + cornerRadius, barY);
        ctx.closePath();
        ctx.fill();
        
        // Add a subtle glow effect if playing
        if (isPlaying && value > 0.5) {
          ctx.save();
          ctx.filter = `blur(${value * 3}px)`;
          ctx.globalAlpha = 0.4;
          ctx.fillRect(barX, barY, adjustedBarWidth, barHeight);
          ctx.restore();
        }
      });
      
      // Draw reflection
      ctx.save();
      ctx.globalAlpha = 0.2;
      ctx.translate(0, dimensions.height);
      ctx.scale(1, -0.2); // Mirror and scale down
      
      data.forEach((value, i) => {
        const barHeight = value * dimensions.height;
        const x = i * barWidth;
        
        const gradient = ctx.createLinearGradient(
          0, 0, 
          0, barHeight
        );
        
        gradient.addColorStop(0, colors.secondary);
        gradient.addColorStop(1, 'transparent');
        
        ctx.fillStyle = gradient;
        ctx.fillRect(x + spacing / 2, 0, adjustedBarWidth, barHeight);
      });
      
      ctx.restore();
      
      // Reset scale for next frame
      ctx.setTransform(1, 0, 0, 1, 0, 0);
      
      // Request next animation frame
      animationRef.current = requestAnimationFrame(draw);
    };
    
    // Start animation loop
    animationRef.current = requestAnimationFrame(draw);
    
    // Clean up animation on unmount
    return () => {
      if (animationRef.current) {
        cancelAnimationFrame(animationRef.current);
      }
    };
  }, [dimensions, isPlaying, theme, audioData]);

  return (
    <Box
      className="visualizer-container"
      borderRadius="xl"
      overflow="hidden"
      bg={getThemeColors().background}
      position="relative"
      height={height}
      boxShadow="0 8px 32px rgba(155, 89, 182, 0.15)"
      transform="rotate(-1deg)"
      _hover={{ transform: "rotate(-1deg) translateY(-5px)" }}
      transition="all 0.3s ease"
    >
      <Box 
        position="absolute" 
        top="0" 
        left="0" 
        right="0" 
        height="3px" 
        bgGradient={`linear(to-r, ${getThemeColors().primary}, ${getThemeColors().secondary})`}
      />
      <canvas 
        ref={canvasRef} 
        style={{ 
          display: 'block', 
          width: '100%', 
          height: '100%',
        }}
      />
      {!isPlaying && (
        <Flex
          position="absolute"
          top="0"
          left="0"
          right="0"
          bottom="0"
          justifyContent="center"
          alignItems="center"
          backdropFilter="blur(1px)"
          transition="all 0.3s ease"
        >
          <Text 
            color={getThemeColors().primary}
            fontSize="sm"
            fontWeight="medium"
            opacity="0.7"
          >
            Waiting for audio...
          </Text>
        </Flex>
      )}
    </Box>
  );
};

export default MusicVisualizer;