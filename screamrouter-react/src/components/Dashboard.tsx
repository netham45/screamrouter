import React, { useState, useEffect, useMemo, useRef } from 'react';
import { useLocation } from 'react-router-dom';
import ApiService, { Source, Sink, Route } from '../api/api';
import VNC from './VNC';
import Equalizer from './Equalizer';
import { ActionButton, SortConfig } from '../utils/commonUtils';
import { useAppContext } from '../context/AppContext';
import SourceList from './SourceList';
import SinkList from './SinkList';
import RouteList from './RouteList';
import AddEditSource from './AddEditSource';
import AddEditSink from './AddEditSink';
import AddEditRoute from './AddEditRoute';
import '../styles/Dashboard.css';
import { CollapsibleSection } from './CollapsibleSection';
import { createActions, Actions } from '../utils/actions';

const Dashboard: React.FC = () => {
    const { listeningToSink, visualizingSink, onListenToSink, onVisualizeSink, setPrimarySource } = useAppContext();
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
    const [showEditModal, setShowEditModal] = useState(false);
    const [selectedItem, setSelectedItem] = useState<any>(null);
    const [selectedItemType, setSelectedItemType] = useState<'sources' | 'sinks' | 'routes' | null>(null);
    const [activeSource, setActiveSource] = useState<string | null>(null);
    const [sortConfig, setSortConfig] = useState<SortConfig>({ key: '', direction: 'asc' });

    const sectionRefs = {
        favoriteSources: useRef<HTMLDivElement>(null),
        favoriteSinks: useRef<HTMLDivElement>(null),
        favoriteRoutes: useRef<HTMLDivElement>(null),
        activeSources: useRef<HTMLDivElement>(null),
        activeSinks: useRef<HTMLDivElement>(null),
        activeRoutes: useRef<HTMLDivElement>(null),
    };

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

    const actions: Actions = useMemo(() => createActions(
      fetchData,
      setError,
      (type, setter) => {
        if (type === 'sources') setStarredSources(setter);
        else if (type === 'sinks') setStarredSinks(setter);
        else if (type === 'routes') setStarredRoutes(setter);
      },
      setShowEqualizerModal,
      setSelectedItem,
      setSelectedItemType,
      setShowVNCModal,
      setActiveSource,
      onListenToSink,
      onVisualizeSink,
      setShowEditModal
    ), [onListenToSink, onVisualizeSink]);

    const toggleSection = (section: keyof typeof expandedSections) => {
      setExpandedSections((prev: typeof expandedSections) => {
        const newState = { ...prev, [section]: !prev[section] };
        localStorage.setItem('dashboardExpandedSections', JSON.stringify(newState));
        return newState;
      });
    };

    const onSort = (key: string) => {
      setSortConfig(prevConfig => ({
        key,
        direction: prevConfig.key === key && prevConfig.direction === 'asc' ? 'desc' : 'asc',
      }));
    };

    const scrollToSection = (sectionKey: keyof typeof sectionRefs) => {
      const sectionRef = sectionRefs[sectionKey];
      if (sectionRef.current) {
        sectionRef.current.scrollIntoView({ behavior: 'smooth' });
        sectionRef.current.classList.add('flash');
        setTimeout(() => {
          sectionRef.current?.classList.remove('flash');
        }, 1000);
      }
    };

    const renderNavigationBar = () => (
      <nav className="dashboard-nav">
        <ActionButton onClick={() => scrollToSection('favoriteSources')}>Favorite Sources</ActionButton>
        <ActionButton onClick={() => scrollToSection('favoriteSinks')}>Favorite Sinks</ActionButton>
        <ActionButton onClick={() => scrollToSection('favoriteRoutes')}>Favorite Routes</ActionButton>
        <ActionButton onClick={() => scrollToSection('activeSources')}>Active Sources</ActionButton>
        <ActionButton onClick={() => scrollToSection('activeSinks')}>Active Sinks</ActionButton>
        <ActionButton onClick={() => scrollToSection('activeRoutes')}>Active Routes</ActionButton>
      </nav>
    );

    const renderEditModal = () => {
      if (!showEditModal || !selectedItem || !selectedItemType) return null;

      const onClose = () => {
        setShowEditModal(false);
        setSelectedItem(null);
        setSelectedItemType(null);
      };

      switch (selectedItemType) {
        case 'sources':
          return <AddEditSource source={selectedItem} onClose={onClose} onSave={fetchData} />;
        case 'sinks':
          return <AddEditSink sink={selectedItem} onClose={onClose} onSave={fetchData} />;
        case 'routes':
          return <AddEditRoute route={selectedItem} onClose={onClose} onSave={fetchData} />;
        default:
          return null;
      }
    };

    return (
      <div className="dashboard">
        <h2>Dashboard</h2>
        {renderNavigationBar()}
        {error && <div className="error-message">{error}</div>}
        <div className="dashboard-content">
          <CollapsibleSection
            title={<><i className="fas fa-microphone"></i> Favorite Sources</>}
            isExpanded={expandedSections.favoriteSources}
            onToggle={() => toggleSection('favoriteSources')}
            id="favorite-sources-header"
            ref={sectionRefs.favoriteSources}
          >
            <SourceList
              sources={sources.filter(source => starredSources.includes(source.name))}
              routes={routes}
              starredSources={starredSources}
              activeSource={activeSource}
              actions={actions}
              sortConfig={sortConfig}
              onSort={onSort}
            />
          </CollapsibleSection>

          <CollapsibleSection
            title={<><i className="fas fa-volume-up"></i> Favorite Sinks</>}
            isExpanded={expandedSections.favoriteSinks}
            onToggle={() => toggleSection('favoriteSinks')}
            id="favorite-sinks-header"
            ref={sectionRefs.favoriteSinks}
          >
            <SinkList
              sinks={sinks.filter(sink => starredSinks.includes(sink.name))}
              routes={routes}
              starredSinks={starredSinks}
              actions={actions}
              listeningToSink={listeningToSink}
              visualizingSink={visualizingSink}
              sortConfig={sortConfig}
              onSort={onSort}
            />
          </CollapsibleSection>

          <CollapsibleSection
            title={<><i className="fas fa-route"></i> Favorite Routes</>}
            isExpanded={expandedSections.favoriteRoutes}
            onToggle={() => toggleSection('favoriteRoutes')}
            id="favorite-routes-header"
            ref={sectionRefs.favoriteRoutes}
          >
            <RouteList
              routes={routes.filter(route => starredRoutes.includes(route.name))}
              starredRoutes={starredRoutes}
              actions={actions}
              sortConfig={sortConfig}
              onSort={onSort}
            />
          </CollapsibleSection>

          <CollapsibleSection
            title={<><i className="fas fa-broadcast-tower"></i> Active Sources</>}
            isExpanded={expandedSections.activeSources}
            onToggle={() => toggleSection('activeSources')}
            id="active-sources-header"
            ref={sectionRefs.activeSources}
          >
            <SourceList
              sources={sources.filter(source => source.enabled)}
              routes={routes}
              starredSources={starredSources}
              activeSource={activeSource}
              actions={actions}
              sortConfig={sortConfig}
              onSort={onSort}
            />
          </CollapsibleSection>

          <CollapsibleSection
            title={<><i className="fas fa-headphones"></i> Active Sinks</>}
            isExpanded={expandedSections.activeSinks}
            onToggle={() => toggleSection('activeSinks')}
            id="active-sinks-header"
            ref={sectionRefs.activeSinks}
          >
            <SinkList
              sinks={sinks.filter(sink => sink.enabled)}
              routes={routes}
              starredSinks={starredSinks}
              actions={actions}
              listeningToSink={listeningToSink}
              visualizingSink={visualizingSink}
              sortConfig={sortConfig}
              onSort={onSort}
            />
          </CollapsibleSection>

          <CollapsibleSection
            title={<><i className="fas fa-exchange-alt"></i> Active Routes</>}
            isExpanded={expandedSections.activeRoutes}
            onToggle={() => toggleSection('activeRoutes')}
            id="active-routes-header"
            ref={sectionRefs.activeRoutes}
          >
            <RouteList
              routes={routes.filter(route => route.enabled)}
              starredRoutes={starredRoutes}
              actions={actions}
              sortConfig={sortConfig}
              onSort={onSort}
            />
          </CollapsibleSection>
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
                type={selectedItemType}
                onClose={() => setShowEqualizerModal(false)}
                onDataChange={fetchData}
              />
            </div>
          </div>
        )}

        {renderEditModal()}
      </div>
    );
};

export default Dashboard;
