import React from 'react';
import { BrowserRouter as Router, Routes, Route } from 'react-router-dom';
import Layout from './components/Layout';
import Dashboard from './components/Dashboard';
import Sources from './components/Sources';
import Sinks from './components/Sinks';
import RoutesComponent from './components/Routes';
import './App.css';

const App: React.FC = () => {
  return (
    <Router basename="/site">
      <Routes>
        <Route path="/" element={<Layout />}>
          <Route index element={<Dashboard />} />
          <Route path="sources" element={<Sources />} />
          <Route path="sinks" element={<Sinks />} />
          <Route path="routes" element={<RoutesComponent />} />
        </Route>
      </Routes>
    </Router>
  );
};

export default App;
