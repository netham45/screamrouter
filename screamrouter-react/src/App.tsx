/**
 * @file App.tsx
 * @description Main entry point for the React application. Sets up routing and context provider.
 */

import React from 'react';
import { BrowserRouter as Router, Routes, Route, useSearchParams, Outlet } from 'react-router-dom'; // Import necessary components from react-router-dom
import { AppProvider, useAppContext } from './context/AppContext'; // Import context provider and hook
import Layout from './components/Layout'; // Import the Layout component
import Dashboard from './components/Dashboard'; // Import the Dashboard component
import Sources from './components/Sources'; // Import the Sources component
import Sinks from './components/Sinks'; // Import the Sinks component
import RoutesComponent from './components/Routes'; // Import the Routes component (renamed to RoutesComponent to avoid conflict with react-router-dom's Route)
import VNC from './components/VNC'; // Import the VNC component
import Visualizer from './components/Visualizer'; // Import the Visualizer component
import Equalizer from './components/Equalizer'; // Import the Equalizer component
import AddEditSource from './components/AddEditSource'; // Import the AddEditSource component
import AddEditSink from './components/AddEditSink'; // Import the AddEditSink component
import AddEditRoute from './components/AddEditRoute'; // Import the AddEditRoute component
import AddEditGroup from './components/AddEditGroup'; // Import the AddEditGroup component
import DesktopMenu from './components/DesktopMenu'; // Import the DesktopMenu component
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
    showEditModal,
    selectedItem,
    selectedItemType,
    setShowEqualizerModal,
    setShowEditModal,
    setSelectedItem,
    setSelectedItemType,
    fetchSources,
    fetchSinks,
    fetchRoutes
  } = useAppContext(); // Access context values and functions

  const handleEditClose = () => {
    setShowEditModal(false); // Hide the edit modal
    setSelectedItem(null); // Clear selected item
    setSelectedItemType(null); // Clear selected item type
  };

  const handleEditSave = () => {
    handleEditClose(); // Close the edit modal and clear selections
    if (selectedItemType === 'sources') fetchSources(); // Fetch updated sources if editing a source
    if (selectedItemType === 'sinks') fetchSinks(); // Fetch updated sinks if editing a sink
    if (selectedItemType === 'routes') fetchRoutes(); // Fetch updated routes if editing a route
    if (selectedItemType === 'group-sink') fetchSinks(); // Fetch updated sinks if editing a group sink
    if (selectedItemType === 'group-source') fetchSources(); // Fetch updated sources if editing a group source
  };

  return (
    <Router basename="/site"> {/* Set the base URL for routing */}
      <Routes> {/* Define routes */}
        <Route path="/desktopmenu" element={<DesktopMenu />} /> {/* Route to DesktopMenu component */}
        <Route path="/" element={<Layout><Outlet /></Layout>}> {/* Layout route with nested routes */}
          <Route index element={<Dashboard />} /> {/* Default route to Dashboard component */}
          <Route path="sources" element={<Sources />} /> {/* Route to Sources component */}
          <Route path="sinks" element={<Sinks />} /> {/* Route to Sinks component */}
          <Route path="routes" element={<RoutesComponent />} /> {/* Route to Routes component */}
        </Route>
        <Route path="/equalizer" element={<EqualizerRoute />} /> {/* Route to EqualizerRoute component */}
        <Route path="/vnc" element={<VNCRoute />} /> {/* Route to VNCRoute component */}
        <Route path="/visualizer" element={<VisualizerRoute />} /> {/* Route to VisualizerRoute component */}
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
      {showEditModal && selectedItemType === 'sources' && (
        <AddEditSource
          source={selectedItem as Source} // Pass the selected source to AddEditSource component
          onClose={handleEditClose} // Function to close the edit modal
          onSave={handleEditSave} // Function to save changes and fetch updated sources
        />
      )}
      {showEditModal && selectedItemType === 'sinks' && (
        <AddEditSink
          sink={selectedItem as Sink} // Pass the selected sink to AddEditSink component
          onClose={handleEditClose} // Function to close the edit modal
          onSave={handleEditSave} // Function to save changes and fetch updated sinks
        />
      )}
      {showEditModal && selectedItemType === 'routes' && (
        <AddEditRoute
          route={selectedItem as RouteType} // Pass the selected route to AddEditRoute component
          onClose={handleEditClose} // Function to close the edit modal
          onSave={handleEditSave} // Function to save changes and fetch updated routes
        />
      )}
      {showEditModal && selectedItemType === 'group-sink' && (
        <AddEditGroup
          type="sink" // Specify the type as sink for AddEditGroup component
          group={selectedItem as Sink} // Pass the selected group sink to AddEditGroup component
          onClose={handleEditClose} // Function to close the edit modal
          onSave={handleEditSave} // Function to save changes and fetch updated sinks
        />
      )}
      {showEditModal && selectedItemType === 'group-source' && (
        <AddEditGroup
          type="source" // Specify the type as source for AddEditGroup component
          group={selectedItem as Source} // Pass the selected group source to AddEditGroup component
          onClose={handleEditClose} // Function to close the edit modal
          onSave={handleEditSave} // Function to save changes and fetch updated sources
        />
      )}
      <audio id="audio" /> {/* Audio element for audio playback */}
    </Router>
  );
};

/**
 * @function App
 * @description Main App component that wraps everything in AppProvider.
 */
const App: React.FC = () => {
  return (
    <AppProvider> {/* Provide context to the entire application */}
      <AppContent /> {/* Render the main content of the application */}
    </AppProvider>
  );
};

export default App; // Export the App component as the default export
