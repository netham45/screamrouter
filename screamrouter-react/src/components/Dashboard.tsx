import React, { useState, useEffect } from 'react';
import { useLocation } from 'react-router-dom';
import ApiService, { Source, Sink, Route } from '../api/api';
import VNC from './VNC';
import Equalizer from './Equalizer';
import ConnectionRenderer from './ConnectionRenderer';
import { ActionButton } from '../utils/commonUtils';
import { useAppContext } from '../context/AppContext';
import SourcesSection from './SourcesSection';
import SinksSection from './SinksSection';
import RoutesSection from './RoutesSection';
import '../styles/Dashboard.css';

const Dashboard: React.FC = () => {
  const { listeningToSink, visualizingSink, onListenToSink, onVisualizeSink } = useAppContext();
  const location = useLocation();
  
  const [sources, setSources] = useState<Source[]>([]);
  const [sinks, setSinks] = useState<Sink[]>([]);
  const [routes, setRoutes] = useState<Route[]>([]);
  const [starredSources, setStarredSources] = useState<string[]>([]);
  const [starredSinks, setStarredSinks] = useState<string[]>([]);
  const [starredRoutes, setStarredRoutes] = useState<string[]>([]);
  const [error, setError] = useState<string | null>(null);
  const [showVNCModal, setShowVNCModal] = useState(false);
  const [showEqualizerModal, setShowEqualizerModal] = useState(false);
  const [selectedItem, setSelectedItem] = useState<any>(null);
  const [selectedItemType, setSelectedItemType] = useState<'sources' | 'sinks' | 'routes' | null>(null);

  const fetchData = async () => {
    try {
      const [sourcesResponse, sinksResponse, routesResponse] = await Promise.all([
        ApiService.getSources(),
        ApiService.getSinks(),
        ApiService.getRoutes()
      ]);
      
      setSources(sourcesResponse.data);
      setSinks(sinksResponse.data);
      setRoutes(routesResponse.data);
      setError(null);
    } catch (error) {
      console.error('Error fetching data:', error);
      setError('Failed to fetch data. Please try again later.');
    }
  };

  useEffect(() => {
    fetchData();
    const starredSourcesData = JSON.parse(localStorage.getItem('starredSources') || '[]');
    const starredSinksData = JSON.parse(localStorage.getItem('starredSinks') || '[]');
    const starredRoutesData = JSON.parse(localStorage.getItem('starredRoutes') || '[]');
    setStarredSources(starredSourcesData);
    setStarredSinks(starredSinksData);
    setStarredRoutes(starredRoutesData);
  }, []);

  useEffect(() => {
    const hash = location.hash;
    if (hash) {
      const element = document.querySelector(hash);
      if (element) {
        element.scrollIntoView({ behavior: 'smooth' });
        element.classList.add('flash');
        setTimeout(() => element.classList.remove('flash'), 1000);
      }
    }
  }, [location]);

  const toggleEnabled = async (type: 'sources' | 'sinks' | 'routes', name: string, currentStatus: boolean) => {
    try {
      const action = currentStatus ? 'disable' : 'enable';
      if (type === 'sources') {
        await (currentStatus ? ApiService.disableSource(name) : ApiService.enableSource(name));
      } else if (type === 'sinks') {
        await (currentStatus ? ApiService.disableSink(name) : ApiService.enableSink(name));
      } else if (type === 'routes') {
        await (currentStatus ? ApiService.disableRoute(name) : ApiService.enableRoute(name));
      }
      await fetchData();
    } catch (error) {
      console.error(`Error toggling ${type} status:`, error);
      setError(`Failed to update ${type} status. Please try again.`);
    }
  };

  const updateVolume = async (type: 'sources' | 'sinks' | 'routes', name: string, volume: number) => {
    try {
      if (type === 'sources') {
        await ApiService.updateSourceVolume(name, volume);
      } else if (type === 'sinks') {
        await ApiService.updateSinkVolume(name, volume);
      } else if (type === 'routes') {
        await ApiService.updateRouteVolume(name, volume);
      }
      await fetchData();
    } catch (error) {
      console.error(`Error updating ${type} volume:`, error);
      setError(`Failed to update ${type} volume. Please try again.`);
    }
  };

  const controlSource = async (sourceName: string, action: 'prevtrack' | 'play' | 'nexttrack') => {
    try {
      await fetch(`/sources/${sourceName}/${action}`, { method: 'GET' });
    } catch (error) {
      console.error(`Error controlling source: ${error}`);
      setError(`Failed to control source. Please try again.`);
    }
  };

  const toggleStar = (type: 'sources' | 'sinks' | 'routes', name: string) => {
    const setStarredItems = {
      sources: setStarredSources,
      sinks: setStarredSinks,
      routes: setStarredRoutes
    }[type];

    setStarredItems(prevItems => {
      const newItems = prevItems.includes(name)
        ? prevItems.filter(item => item !== name)
        : [...prevItems, name];
      localStorage.setItem(`starred${type.charAt(0).toUpperCase() + type.slice(1)}`, JSON.stringify(newItems));
      return newItems;
    });
  };

  const getRoutesForSource = (sourceName: string) => {
    return routes.filter(route => route.source === sourceName && route.enabled);
  };

  const getRoutesForSink = (sinkName: string) => {
    return routes.filter(route => route.sink === sinkName && route.enabled);
  };

  const getGroupMembers = (name: string, type: 'sources' | 'sinks') => {
    const items = type === 'sources' ? sources : sinks;
    const item = items.find(i => i.name === name);
    return item && item.is_group ? item.group_members.join(', ') : '';
  };

  const scrollToSection = (sectionId: string) => {
    const section = document.getElementById(sectionId);
    if (section) {
      section.scrollIntoView({ behavior: 'smooth' });
    }
  };

  const renderNavigationBar = () => (
    <nav className="dashboard-nav">
      <ActionButton onClick={() => scrollToSection('favorite-sources')}>Favorite Sources</ActionButton>
      <ActionButton onClick={() => scrollToSection('favorite-sinks')}>Favorite Sinks</ActionButton>
      <ActionButton onClick={() => scrollToSection('favorite-routes')}>Favorite Routes</ActionButton>
      <ActionButton onClick={() => scrollToSection('active-sources')}>Active Sources</ActionButton>
      <ActionButton onClick={() => scrollToSection('active-sinks')}>Active Sinks</ActionButton>
      <ActionButton onClick={() => scrollToSection('active-routes')}>Active Routes</ActionButton>
    </nav>
  );

  const jumpToAnchor = (name: string) => {
    const element = document.getElementById(`${name}`);
    if (element) {
      element.scrollIntoView({ behavior: 'smooth' });
      element.classList.add('flash');
      setTimeout(() => element.classList.remove('flash'), 1000);
    }
  };

  return (
    <div className="dashboard">
      <h2>Dashboard</h2>
      {renderNavigationBar()}
      {error && <div className="error-message">{error}</div>}
      <div className="dashboard-content">
        <div className="dashboard-sections">
          <div className="left-column">
            <SourcesSection
              title={<><i className="fas fa-microphone"></i> Favorite Sources</>}
              sources={sources}
              routes={routes}
              starredSources={starredSources}
              toggleEnabled={toggleEnabled}
              toggleStar={toggleStar}
              updateVolume={updateVolume}
              controlSource={controlSource}
              setSelectedItem={setSelectedItem}
              setShowVNCModal={setShowVNCModal}
              setShowEqualizerModal={setShowEqualizerModal}
              setSelectedItemType={setSelectedItemType}
              getRoutesForSource={getRoutesForSource}
              getGroupMembers={getGroupMembers}
              jumpToAnchor={jumpToAnchor}
              isFavoriteView={true}
              onDataChange={fetchData}
            />
            <SinksSection
              title={<><i className="fas fa-volume-up"></i> Favorite Sinks</>}
              sinks={sinks}
              routes={routes}
              starredSinks={starredSinks}
              toggleEnabled={toggleEnabled}
              toggleStar={toggleStar}
              updateVolume={updateVolume}
              setSelectedItem={setSelectedItem}
              setShowEqualizerModal={setShowEqualizerModal}
              setSelectedItemType={setSelectedItemType}
              getRoutesForSink={getRoutesForSink}
              getGroupMembers={getGroupMembers}
              jumpToAnchor={jumpToAnchor}
              isFavoriteView={true}
              onListenToSink={onListenToSink}
              onVisualizeSink={onVisualizeSink}
              listeningToSink={listeningToSink}
              visualizingSink={visualizingSink}
              onDataChange={fetchData}
            />
            <RoutesSection
              title={<><i className="fas fa-route"></i> Favorite Routes</>}
              routes={routes}
              sources={sources}
              sinks={sinks}
              starredRoutes={starredRoutes}
              toggleEnabled={toggleEnabled}
              toggleStar={toggleStar}
              updateVolume={updateVolume}
              setSelectedItem={setSelectedItem}
              setShowEqualizerModal={setShowEqualizerModal}
              setSelectedItemType={setSelectedItemType}
              jumpToAnchor={jumpToAnchor}
              isFavoriteView={true}
              onDataChange={fetchData}
            />
          </div>
          <div className="right-column">
            <SourcesSection
              title={<><i className="fas fa-broadcast-tower"></i> Active Sources</>}
              sources={sources}
              routes={routes}
              toggleEnabled={toggleEnabled}
              updateVolume={updateVolume}
              controlSource={controlSource}
              setSelectedItem={setSelectedItem}
              setShowVNCModal={setShowVNCModal}
              setShowEqualizerModal={setShowEqualizerModal}
              setSelectedItemType={setSelectedItemType}
              getRoutesForSource={getRoutesForSource}
              getGroupMembers={getGroupMembers}
              jumpToAnchor={jumpToAnchor}
              isFavoriteView={false}
              onDataChange={fetchData}
            />
            <SinksSection
              title={<><i className="fas fa-headphones"></i> Active Sinks</>}
              sinks={sinks}
              routes={routes}
              toggleEnabled={toggleEnabled}
              updateVolume={updateVolume}
              setSelectedItem={setSelectedItem}
              setShowEqualizerModal={setShowEqualizerModal}
              setSelectedItemType={setSelectedItemType}
              getRoutesForSink={getRoutesForSink}
              getGroupMembers={getGroupMembers}
              jumpToAnchor={jumpToAnchor}
              isFavoriteView={false}
              onListenToSink={onListenToSink}
              onVisualizeSink={onVisualizeSink}
              listeningToSink={listeningToSink}
              visualizingSink={visualizingSink}
              onDataChange={fetchData}
            />
            <RoutesSection
              title={<><i className="fas fa-exchange-alt"></i> Active Routes</>}
              routes={routes}
              sources={sources}
              sinks={sinks}
              toggleEnabled={toggleEnabled}
              updateVolume={updateVolume}
              setSelectedItem={setSelectedItem}
              setShowEqualizerModal={setShowEqualizerModal}
              setSelectedItemType={setSelectedItemType}
              jumpToAnchor={jumpToAnchor}
              isFavoriteView={false}
              onDataChange={fetchData}
            />
          </div>
        </div>
        <div className="connection-renderer-container">
          <ConnectionRenderer routes={routes} />
        </div>
      </div>

      {showVNCModal && selectedItem && (
        <div className="modal-overlay">
          <div className="modal-content">
            <ActionButton className="close-modal" onClick={() => setShowVNCModal(false)}>×</ActionButton>
            <VNC source={selectedItem} />
          </div>
        </div>
      )}

      {showEqualizerModal && selectedItem && selectedItemType && (
        <div className="modal-overlay">
          <div className="modal-content">
            <ActionButton className="close-modal" onClick={() => setShowEqualizerModal(false)}>×</ActionButton>
            <Equalizer
              item={selectedItem}
              type={selectedItemType === 'routes' ? 'routes' : selectedItemType}
              onClose={() => setShowEqualizerModal(false)}
              onDataChange={fetchData}
            />
          </div>
        </div>
      )}
    </div>
  );
};

export default Dashboard;