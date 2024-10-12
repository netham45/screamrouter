import React from 'react';
import { BrowserRouter as Router, Routes, Route } from 'react-router-dom';
import { AppProvider, useAppContext } from './context/AppContext';
import Layout from './components/Layout';
import Dashboard from './components/Dashboard';
import Sources from './components/Sources';
import Sinks from './components/Sinks';
import RoutesComponent from './components/Routes';
import AudioVisualizer from './components/AudioVisualizer';
import VNC from './components/VNC';
import Equalizer from './components/Equalizer';
import AddEditSource from './components/AddEditSource';
import AddEditSink from './components/AddEditSink';
import AddEditRoute from './components/AddEditRoute';
import AddEditGroup from './components/AddEditGroup';
import './App.css';
import './styles/darkMode.css';

const AppContent: React.FC = () => {
  const { 
    listeningToSink, 
    visualizingSink, 
    showVNCModal, 
    showEqualizerModal,
    showEditModal,
    selectedItem,
    selectedItemType,
    setShowVNCModal,
    setShowEqualizerModal,
    setShowEditModal,
    setSelectedItem,
    setSelectedItemType,
    fetchSources,
    fetchSinks,
    fetchRoutes
  } = useAppContext();

  const handleEditClose = () => {
    setShowEditModal(false);
    setSelectedItem(null);
    setSelectedItemType(null);
  };

  const handleEditSave = () => {
    handleEditClose();
    if (selectedItemType === 'sources') fetchSources();
    if (selectedItemType === 'sinks') fetchSinks();
    if (selectedItemType === 'routes') fetchRoutes();
    if (selectedItemType === 'group-sink') fetchSinks();
    if (selectedItemType === 'group-source') fetchSources();
  };

  return (
    <Router basename="/site">
      <Layout>
        <Routes>
          <Route index element={<Dashboard />} />
          <Route path="sources" element={<Sources />} />
          <Route path="sinks" element={<Sinks />} />
          <Route path="routes" element={<RoutesComponent />} />
        </Routes>
      </Layout>
      <AudioVisualizer 
        listeningToSink={listeningToSink?.name || null}
        visualizingSink={visualizingSink?.name || null}
        sinkIp={visualizingSink?.ip || listeningToSink?.ip || null}
      />
      {showVNCModal && selectedItem && (
        <VNC source={selectedItem} />
      )}
      {showEqualizerModal && selectedItem && selectedItemType && (
        <Equalizer
          item={selectedItem}
          type={selectedItemType as 'sources' | 'sinks' | 'routes'}
          onClose={() => {
            setShowEqualizerModal(false);
            setSelectedItem(null);
          }}
          onDataChange={() => {
            if (selectedItemType === 'sources') fetchSources();
            if (selectedItemType === 'sinks') fetchSinks();
            if (selectedItemType === 'routes') fetchRoutes();
          }}
        />
      )}
      {showEditModal && selectedItemType === 'sources' && (
        <AddEditSource
          source={selectedItem}
          onClose={handleEditClose}
          onSave={handleEditSave}
        />
      )}
      {showEditModal && selectedItemType === 'sinks' && (
        <AddEditSink
          sink={selectedItem}
          onClose={handleEditClose}
          onSave={handleEditSave}
        />
      )}
      {showEditModal && selectedItemType === 'routes' && (
        <AddEditRoute
          route={selectedItem}
          onClose={handleEditClose}
          onSave={handleEditSave}
        />
      )}
      {showEditModal && selectedItemType === 'group-sink' && (
        <AddEditGroup
          type="sink"
          group={selectedItem}
          onClose={handleEditClose}
          onSave={handleEditSave}
        />
      )}
      {showEditModal && selectedItemType === 'group-source' && (
        <AddEditGroup
          type="source"
          group={selectedItem}
          onClose={handleEditClose}
          onSave={handleEditSave}
        />
      )}
    </Router>
  );
};

const App: React.FC = () => {
  return (
    <AppProvider>
      <AppContent />
    </AppProvider>
  );
};

export default App;
