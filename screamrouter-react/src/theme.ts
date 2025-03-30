import { extendTheme } from '@chakra-ui/react';

// Define the colors that match the current application
const colors = {
  brand: {
    50: '#e6f7ef',
    100: '#c3ecd8',
    200: '#9fe0c0',
    300: '#7bd4a8',
    400: '#57c890',
    500: '#2ecc71', // Primary green color from the current theme
    600: '#27ae60', // Darker green for hover states
    700: '#208e4e',
    800: '#196f3d',
    900: '#11502b',
  },
  secondary: {
    50: '#e8f4f8',
    100: '#c7e5ee',
    200: '#a5d5e4',
    300: '#84c5da',
    400: '#62b6d0',
    500: '#3498db', // Primary blue color from the current theme
    600: '#2980b9', // Darker blue for hover states
    700: '#216897',
    800: '#195074',
    900: '#123852',
  },
  dark: {
    50: '#e6e8ea',
    100: '#c1c6cb',
    200: '#9ba4ab',
    300: '#75828c',
    400: '#4f606c',
    500: '#34495e', // Dark blue from the current theme
    600: '#2c3e50', // Darker blue for headers
    700: '#243342',
    800: '#1c2833',
    900: '#141d25',
  },
  error: {
    500: '#e74c3c', // Red color from the current theme
  },
  // New vibrant music-themed colors
  music: {
    purple: '#8E44AD', // Deep purple
    violet: '#9B59B6', // Vibrant violet
    pink: '#E91E63', // Energetic pink
    orange: '#FF5722', // Bright orange
    yellow: '#F1C40F', // Sunny yellow
    aqua: '#1ABC9C', // Fresh aqua
    lime: '#CDDC39', // Electric lime
  },
  // Gradient definitions for use in CSS
  gradients: {
    primary: 'linear-gradient(45deg, #9B59B6, #3498db)',
    secondary: 'linear-gradient(135deg, #FF5722, #F1C40F)',
    accent: 'linear-gradient(225deg, #1ABC9C, #2ecc71)',
  }
};

// Define the global styles
const styles = {
  global: (props: { colorMode: string }) => ({
    body: {
      bg: props.colorMode === 'dark' ? 'gray.900' : 'gray.50',
      color: props.colorMode === 'dark' ? 'gray.100' : 'dark.600',
      transition: 'background-color 0.3s ease',
    },
  }),
};

// Define component styles
const components = {
  Button: {
    baseStyle: {
      fontWeight: 'normal',
      borderRadius: '4px',
      transition: 'all 0.3s ease',
      _hover: {
        //transform: 'translateY(-2px)',
        boxShadow: '0 4px 12px rgba(0, 0, 0, 0.15)',
      },
    },
    variants: {
      solid: (props: { colorMode: string }) => ({
        bg: props.colorMode === 'dark' ? 'secondary.600' : 'secondary.500',
        color: 'white',
        _hover: {
          bg: props.colorMode === 'dark' ? 'secondary.700' : 'secondary.600',
        },
      }),
      primary: (props: { colorMode: string }) => ({
        bg: props.colorMode === 'dark' ? 'brand.600' : 'brand.500',
        color: 'white',
        _hover: {
          bg: props.colorMode === 'dark' ? 'brand.700' : 'brand.600',
        },
      }),
      error: {
        bg: 'error.500',
        color: 'white',
      },
      ghost: (props: { colorMode: string }) => ({
        bg: 'transparent',
        color: props.colorMode === 'dark' ? 'gray.200' : 'gray.700',
        _hover: {
          bg: props.colorMode === 'dark' ? 'whiteAlpha.200' : 'blackAlpha.100',
        },
      }),
      // New music-themed button variant
      music: {
        bgGradient: 'linear-gradient(45deg, #9B59B6, #3498db)',
        color: 'white',
        borderRadius: '25px',
        _hover: {
          bgGradient: 'linear-gradient(45deg, #8E44AD, #2980b9)',
          transform: 'scale(1.05)',
        },
      },
    },
    defaultProps: {
      variant: 'solid',
    },
  },
  Heading: {
    baseStyle: (props: { colorMode: string }) => ({
      color: props.colorMode === 'dark' ? 'gray.100' : 'dark.600',
    }),
  },
  Table: {
    variants: {
      simple: (props: { colorMode: string }) => ({
        th: {
          bg: props.colorMode === 'dark' ? 'gray.700' : 'gray.100',
          color: props.colorMode === 'dark' ? 'gray.100' : 'dark.600',
          fontWeight: 'bold',
        },
        tr: {
          _hover: {
            bg: props.colorMode === 'dark' ? 'gray.600' : 'gray.50',
          },
        },
        td: {
          borderBottom: '1px solid',
          borderColor: props.colorMode === 'dark' ? 'gray.600' : 'gray.200',
        },
      }),
    },
    defaultProps: {
      variant: 'simple',
    },
  },
  Box: {
    baseStyle: (props: { colorMode: string }) => ({
      bg: props.colorMode === 'dark' ? 'gray.800' : 'white',
      borderRadius: '8px',
      transition: 'all 0.3s ease',
    }),
  },
  Flex: {
    baseStyle: (props: { colorMode: string }) => ({
      bg: props.colorMode === 'dark' ? 'gray.800' : 'white',
    }),
  },
  Card: {
    baseStyle: {
      container: {
        borderRadius: '12px',
        overflow: 'hidden',
        transition: 'all 0.3s ease',
        _hover: {
          //transform: 'translateY(-4px)',
          boxShadow: '0 8px 16px rgba(0, 0, 0, 0.1)',
        },
      },
    },
  },
};

// Create the theme
const theme = extendTheme({
  colors,
  styles,
  components,
  fonts: {
    body: "'Poppins', system-ui, sans-serif",
    heading: "'Poppins', system-ui, sans-serif",
  },
  config: {
    initialColorMode: 'light',
    useSystemColorMode: false,
  },
  // Custom theme additions for music app feel
  shadows: {
    outline: '0 0 0 3px rgba(62, 152, 199, 0.6)',
  },
  radii: {
    sm: '4px',
    md: '8px',
    lg: '12px',
    xl: '16px',
    '2xl': '24px',
    full: '9999px',
  },
});

export default theme;