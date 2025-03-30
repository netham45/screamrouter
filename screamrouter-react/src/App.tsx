/**
 * @file App.tsx
 * @description Main entry point for the React application. Sets up routing and context provider.
 */

import React from 'react';
import { BrowserRouter as Router, Routes, Route, useSearchParams } from 'react-router-dom'; // Import necessary components from react-router-dom
import { ChakraProvider } from '@chakra-ui/react'; // Import ChakraProvider
import theme from './theme'; // Import custom theme
import { AppProvider, useAppContext } from './context/AppContext'; // Import context provider and hook
import VNC from './components/pages/VNCPage'; // Import the VNC component
import Visualizer from './components/pages/VisualizerPage'; // Import the Visualizer component
import Equalizer from './components/pages/EqualizerPage'; // Import the Equalizer component
import AddEditSourcePage from './components/pages/AddEditSourcePage'; // Import the AddEditSourcePage component
import AddEditSinkPage from './components/pages/AddEditSinkPage'; // Import the AddEditSinkPage component
import AddEditRoutePage from './components/pages/AddEditRoutePage'; // Import the AddEditRoutePage component
import AddEditGroupPage from './components/pages/AddEditGroupPage'; // Import the AddEditGroupPage component
import { DesktopMenu } from './components/desktopMenu'; // Import DesktopMenu
import { ColorProvider } from './components/desktopMenu/context/ColorContext'; // Import ColorProvider
import FullMenu from './components/FullMenu'; // Import the FullMenu component
import GlobalFunctionsComponent from './GlobalFunctionsComponent'; // Import the GlobalFunctionsComponent
import './App.css'; // Import global styles
import './styles/darkMode.css'; // Import dark mode styles
import { Source, Sink, Route as RouteType } from './api/api'; // Import types for Source, Sink, and Route

/**
 * @function EqualizerRoute
 * @description Component to handle Equalizer route with search parameters.
 */
const EqualizerRoute: React.FC = () => {
  const [searchParams] = useSearchParams(); // Hook to access URL search parameters
  const { sources, sinks, routes } = useAppContext(); // Access context values for sources, sinks, and routes

  const type = searchParams.get('type') as 'sources' | 'sinks' | 'routes' | 'group-sink' | 'group-source'; // Get the 'type' parameter
  const name = searchParams.get('name'); // Get the 'name' parameter

  let item: Source | Sink | RouteType | null = null; // Initialize item variable to hold the selected source, sink, or route
  if (type && name) {
    if (type === 'sources' || type === 'group-source') item = sources.find(s => s.name === name) || null; // Find source by name
    else if (type === 'sinks' || type === 'group-sink') item = sinks.find(s => s.name === name) || null; // Find sink by name
    else if (type === 'routes') item = routes.find(r => r.name === name) || null; // Find route by name
  }

  if (!item) return <div>Item not found</div>; // Return error message if item is not found

  return (
    <Equalizer
      item={item} // Pass the selected item to Equalizer component
      type={type} // Pass the type of item to Equalizer component
      onClose={() => window.close()} // Function to close the window when equalizer is closed
      onDataChange={() => {/* Handle data change */}} // Placeholder for handling data changes
    />
  );
};

/**
 * @function VNCRoute
 * @description Component to handle VNC route with search parameters.
 */
const VNCRoute: React.FC = () => {
  const [searchParams] = useSearchParams(); // Hook to access URL search parameters
  const { sources } = useAppContext(); // Access context values for sources

  const ip = searchParams.get('ip'); // Get the 'ip' parameter
  const port = searchParams.get('port'); // Get the 'port' parameter

  const source = sources.find(s => s.vnc_ip === ip && s.vnc_port === Number(port)); // Find source by IP and port

  if (!source) return <div>Source not found</div>; // Return error message if source is not found

  return <VNC source={source} onClose={() => window.close()} />; // Pass the selected source to VNC component
};

/**
 * @function VisualizerRoute
 * @description Component to handle Visualizer route with search parameters.
 */
const VisualizerRoute: React.FC = () => {
  const [searchParams] = useSearchParams(); // Hook to access URL search parameters
  const ip = searchParams.get('ip'); // Get the 'ip' parameter

  if (!ip) return <div>IP not provided</div>; // Return error message if IP is not provided

  return <Visualizer ip={ip} />; // Pass the IP to Visualizer component
};

/**
 * @function AppContent
 * @description Main content of the application wrapped in Router and Routes.
 */
const AppContent: React.FC = () => {
  const { 
    showEqualizerModal,
    selectedItem,
    selectedItemType,
    setShowEqualizerModal,
    setSelectedItem,
    fetchSources,
    fetchSinks,
    fetchRoutes
  } = useAppContext(); // Access context values and functions


  return (
    <Router basename="/site"> {/* Set the base URL for routing */}
      {/* Include the GlobalFunctionsComponent in the React tree */}
      <GlobalFunctionsComponent />
      
      <Routes> {/* Define routes */}
        <Route path="/desktopmenu" element={<ColorProvider><DesktopMenu /></ColorProvider>} /> {/* Route to DesktopMenu with color context */}
        <Route path="/" element={<FullMenu />} /> {/* Root route now uses FullMenu component */}
        <Route path="/equalizer" element={<EqualizerRoute />} /> {/* Route to EqualizerRoute component */}
        <Route path="/vnc" element={<VNCRoute />} /> {/* Route to VNCRoute component */}
        <Route path="/visualizer" element={<VisualizerRoute />} /> {/* Route to VisualizerRoute component */}
        <Route path="/add-source" element={<AddEditSourcePage />} /> {/* Route to AddEditSourcePage component */}
        <Route path="/edit-source" element={<AddEditSourcePage />} /> {/* Route to AddEditSourcePage component */}
        <Route path="/add-sink" element={<AddEditSinkPage />} /> {/* Route to AddEditSinkPage component */}
        <Route path="/edit-sink" element={<AddEditSinkPage />} /> {/* Route to AddEditSinkPage component */}
        <Route path="/add-route" element={<AddEditRoutePage />} /> {/* Route to AddEditRoutePage component */}
        <Route path="/edit-route" element={<AddEditRoutePage />} /> {/* Route to AddEditRoutePage component */}
        <Route path="/add-group" element={<AddEditGroupPage />} /> {/* Route to AddEditGroupPage component */}
        <Route path="/edit-group" element={<AddEditGroupPage />} /> {/* Route to AddEditGroupPage component */}
      </Routes>
      
      {showEqualizerModal && selectedItem && selectedItemType && (
        <Equalizer
          item={selectedItem as Source | Sink | RouteType} // Pass the selected item to Equalizer component
          type={selectedItemType as 'sources' | 'sinks' | 'routes' | 'group-sink' | 'group-source'} // Pass the type of item to Equalizer component
          onClose={() => {
            setShowEqualizerModal(false); // Hide the equalizer modal
            setSelectedItem(null); // Clear selected item
          }}
          onDataChange={() => {
            if (selectedItemType === 'sources') fetchSources(); // Fetch updated sources on data change
            if (selectedItemType === 'sinks') fetchSinks(); // Fetch updated sinks on data change
            if (selectedItemType === 'routes') fetchRoutes(); // Fetch updated routes on data change
          }}
        />
      )}
      <audio id="audio" /> {/* Audio element for audio playback */}
    </Router>
  );
};

/**
 * @function App
 * @description Main App component that wraps everything in ChakraProvider and AppProvider.
 */
const App: React.FC = () => {
  return (
    <ChakraProvider theme={theme}> {/* Provide Chakra UI theme to the entire application */}
      <AppProvider> {/* Provide context to the entire application */}
        <AppContent /> {/* Render the main content of the application */}
      </AppProvider>
    </ChakraProvider>
  );
};

export default App; // Export the App component as the default export
