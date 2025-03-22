/**
 * Main entry point for the React application.
 * This file is responsible for rendering the root App component and setting up performance monitoring.
 */

import React from 'react';
import { createRoot } from 'react-dom/client';
import { ChakraProvider } from '@chakra-ui/react';
import theme from './theme';
import App from './App';
import reportWebVitals from './reportWebVitals';
import './globalFunctions'; // Import globalFunctions to expose functions on window object

// Find the root element in the DOM
const container = document.getElementById('root');

// Ensure the root element exists before rendering
if (container) {
  // Create a root for the React app
  const root = createRoot(container);

  // Render the App component within ChakraProvider for Chakra UI theming
  root.render(
    <React.StrictMode>
      <ChakraProvider theme={theme}>
        <App />
      </ChakraProvider>
    </React.StrictMode>
  );
}

// Set up performance monitoring with Web Vitals
// To start measuring performance, pass a function to log results
// (for example: reportWebVitals(console.log))
// or send to an analytics endpoint. Learn more: https://bit.ly/CRA-vitals
reportWebVitals();

// Note: You can customize the reportWebVitals call to send data to your preferred analytics service
// For example:
// reportWebVitals(metrics => {
//   // Send metrics to your analytics service here
//   console.log(metrics);
// });
