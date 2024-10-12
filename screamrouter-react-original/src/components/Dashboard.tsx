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

// Import CollapsibleSection from Layout
import { CollapsibleSection } from './Layout';

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
  
    // Initialize expanded sections state from LocalStorage
    const [expandedSections, setExpandedSections] = useState(() => {
      const savedState = localStorage.getItem('dashboardExpandedSections');
      return savedState ? JSON.parse(savedState) : {
        favoriteSources: true,
        favoriteSinks: true,
        favoriteRoutes: true,
        activeSources: true,
        activeSinks: true,
        activeRoutes: true,
      };
    });
  
    const toggleSection = (section: keyof typeof expandedSections) => {
      setExpandedSections(prev => {
        const newState = { ...prev, [section]: !prev[section] };
        localStorage.setItem('dashboardExpandedSections', JSON.stringify(newState));
        return newState;
      });
    };
  
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
      section.classList.add('flash');
      setTimeout(() => section.classList.remove('flash'), 1000);
    }
  };

  const renderNavigationBar = () => (
    <nav className="dashboard-nav">
      <ActionButton onClick={() => scrollToSection('favorite-sources-header')}>Favorite Sources</ActionButton>
      <ActionButton onClick={() => scrollToSection('favorite-sinks-header')}>Favorite Sinks</ActionButton>
      <ActionButton onClick={() => scrollToSection('favorite-routes-header')}>Favorite Routes</ActionButton>
      <ActionButton onClick={() => scrollToSection('active-sources-header')}>Active Sources</ActionButton>
      <ActionButton onClick={() => scrollToSection('active-sinks-header')}>Active Sinks</ActionButton>
      <ActionButton onClick={() => scrollToSection('active-routes-header')}>Active Routes</ActionButton>
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
            <CollapsibleSection
              title={<><i className="fas fa-microphone"></i> Favorite Sources</>}
              isExpanded={expandedSections.favoriteSources}
              onToggle={() => toggleSection('favoriteSources')}
              id="favorite-sources-header"
            >
              <SourcesSection
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
            </CollapsibleSection>

            <CollapsibleSection
              title={<><i className="fas fa-volume-up"></i> Favorite Sinks</>}
              isExpanded={expandedSections.favoriteSinks}
              onToggle={() => toggleSection('favoriteSinks')}
              id="favorite-sinks-header"
            >
              <SinksSection
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
            </CollapsibleSection>

            <CollapsibleSection
              title={<><i className="fas fa-route"></i> Favorite Routes</>}
              isExpanded={expandedSections.favoriteRoutes}
              onToggle={() => toggleSection('favoriteRoutes')}
              id="favorite-routes-header"
            >
              <RoutesSection
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
            </CollapsibleSection>
          </div>
          <div className="right-column">
            <CollapsibleSection
              title={<><i className="fas fa-broadcast-tower"></i> Active Sources</>}
              isExpanded={expandedSections.activeSources}
              onToggle={() => toggleSection('activeSources')}
              id="active-sources-header"
            >
              <SourcesSection
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
            </CollapsibleSection>

            <CollapsibleSection
              title={<><i className="fas fa-headphones"></i> Active Sinks</>}
              isExpanded={expandedSections.activeSinks}
              onToggle={() => toggleSection('activeSinks')}
              id="active-sinks-header"
            >
              <SinksSection
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
            </CollapsibleSection>

            <CollapsibleSection
              title={<><i className="fas fa-exchange-alt"></i> Active Routes</>}
              isExpanded={expandedSections.activeRoutes}
              onToggle={() => toggleSection('activeRoutes')}
              id="active-routes-header"
            >
              <RoutesSection
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
            </CollapsibleSection>
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