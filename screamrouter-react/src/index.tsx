import React from 'react';
import { createRoot } from 'react-dom/client';
import './styles/index.css';
import App from './App';
import reportWebVitals from './reportWebVitals';

/**
 * Main entry point for the React application
 * This file is responsible for rendering the root App component and setting up performance monitoring
 */

// Find the root element in the DOM
const container = document.getElementById('root');

// Ensure the root element exists before rendering
if (container) {
  // Create a root for the React app
  const root = createRoot(container);

  // Render the App component within StrictMode for additional checks and warnings
  root.render(
    <React.StrictMode>
      <App />
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
