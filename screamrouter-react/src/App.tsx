import React from 'react';
import { BrowserRouter as Router, Routes, Route } from 'react-router-dom';
import { AppProvider, useAppContext } from './context/AppContext';
import Layout from './components/Layout';
import Dashboard from './components/Dashboard';
import Sources from './components/Sources';
import Sinks from './components/Sinks';
import RoutesComponent from './components/Routes';
import AudioVisualizer from './components/AudioVisualizer';
import './App.css';
import './styles/darkMode.css';

const AppContent: React.FC = () => {
  const { listeningToSink, visualizingSink, onListenToSink, onVisualizeSink } = useAppContext();

  return (
    <Router basename="/site">
      <Layout>
        <Routes>
          <Route index element={
            <Dashboard />
          } />
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
