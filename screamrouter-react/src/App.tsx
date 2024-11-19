import React from 'react';
import { BrowserRouter as Router, Routes, Route, useSearchParams, Outlet } from 'react-router-dom';
import { AppProvider, useAppContext } from './context/AppContext';
import Layout from './components/Layout';
import Dashboard from './components/Dashboard';
import Sources from './components/Sources';
import Sinks from './components/Sinks';
import RoutesComponent from './components/Routes';
import VNC from './components/VNC';
import Visualizer from './components/Visualizer';
import Equalizer from './components/Equalizer';
import AddEditSource from './components/AddEditSource';
import AddEditSink from './components/AddEditSink';
import AddEditRoute from './components/AddEditRoute';
import AddEditGroup from './components/AddEditGroup';
import DesktopMenu from './components/DesktopMenu';
import './App.css';
import './styles/darkMode.css';
import { Source, Sink, Route as RouteType } from './api/api';

const EqualizerRoute: React.FC = () => {
  const [searchParams] = useSearchParams();
  const { sources, sinks, routes } = useAppContext();

  const type = searchParams.get('type') as 'sources' | 'sinks' | 'routes' | 'group-sink' | 'group-source';
  const name = searchParams.get('name');

  let item: Source | Sink | RouteType | null = null;
  if (type && name) {
    if (type === 'sources' || type === 'group-source') item = sources.find(s => s.name === name) || null;
    else if (type === 'sinks' || type === 'group-sink') item = sinks.find(s => s.name === name) || null;
    else if (type === 'routes') item = routes.find(r => r.name === name) || null;
  }

  if (!item) return <div>Item not found</div>;

  return (
    <Equalizer
      item={item}
      type={type}
      onClose={() => window.close()}
      onDataChange={() => {/* Handle data change */}}
    />
  );
};

const VNCRoute: React.FC = () => {
  const [searchParams] = useSearchParams();
  const { sources } = useAppContext();

  const ip = searchParams.get('ip');
  const port = searchParams.get('port');

  const source = sources.find(s => s.vnc_ip === ip && s.vnc_port === Number(port));

  if (!source) return <div>Source not found</div>;

  return <VNC source={source} onClose={() => window.close()} />;
};

const VisualizerRoute: React.FC = () => {
  const [searchParams] = useSearchParams();
  const ip = searchParams.get('ip');

  if (!ip) return <div>IP not provided</div>;

  return <Visualizer ip={ip} />;
};

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
      <Routes>
        <Route path="/desktopmenu" element={<DesktopMenu />} />
        <Route path="/" element={<Layout><Outlet /></Layout>}>
          <Route index element={<Dashboard />} />
          <Route path="sources" element={<Sources />} />
          <Route path="sinks" element={<Sinks />} />
          <Route path="routes" element={<RoutesComponent />} />
        </Route>
        <Route path="/equalizer" element={<EqualizerRoute />} />
        <Route path="/vnc" element={<VNCRoute />} />
        <Route path="/visualizer" element={<VisualizerRoute />} />
      </Routes>
      
      {showEqualizerModal && selectedItem && selectedItemType && (
        <Equalizer
          item={selectedItem as Source | Sink | RouteType}
          type={selectedItemType as 'sources' | 'sinks' | 'routes' | 'group-sink' | 'group-source'}
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
          source={selectedItem as Source}
          onClose={handleEditClose}
          onSave={handleEditSave}
        />
      )}
      {showEditModal && selectedItemType === 'sinks' && (
        <AddEditSink
          sink={selectedItem as Sink}
          onClose={handleEditClose}
          onSave={handleEditSave}
        />
      )}
      {showEditModal && selectedItemType === 'routes' && (
        <AddEditRoute
          route={selectedItem as RouteType}
          onClose={handleEditClose}
          onSave={handleEditSave}
        />
      )}
      {showEditModal && selectedItemType === 'group-sink' && (
        <AddEditGroup
          type="sink"
          group={selectedItem as Sink}
          onClose={handleEditClose}
          onSave={handleEditSave}
        />
      )}
      {showEditModal && selectedItemType === 'group-source' && (
        <AddEditGroup
          type="source"
          group={selectedItem as Source}
          onClose={handleEditClose}
          onSave={handleEditSave}
        />
      )}
      <audio id="audio" />
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
