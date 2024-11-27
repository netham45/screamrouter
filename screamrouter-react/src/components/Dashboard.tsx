/**
 * React component for the Dashboard.
 * This component serves as the main interface for managing sources, sinks, and routes.
 * It includes collapsible sections for favorite and active items, navigation buttons,
 * and modals for editing and equalizing items.
 *
 * @param {React.FC} props - The properties for the component. No specific props are required.
 */
import React, { useState, useEffect, useMemo, useRef } from 'react';
import { Source, Sink, Route } from '../api/api';
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

/**
 * Type definition for the selected item type.
 */
type SelectedItemType = Source | Sink | Route | null;

const Dashboard: React.FC = () => {
    /**
     * Context variables and functions provided by AppContext.
     */
    const { 
      listeningToSink, 
      visualizingSink, 
      onListenToSink, 
      onVisualizeSink,
      sources,
      sinks,
      routes,
      activeSource: contextActiveSource,
      onToggleActiveSource,
      openVNCModal,
      navigateToItem
    } = useAppContext();

    /**
     * State to keep track of starred sources.
     */
    const [starredSources, setStarredSources] = useState<string[]>([]);

    /**
     * State to keep track of starred sinks.
     */
    const [starredSinks, setStarredSinks] = useState<string[]>([]);

    /**
     * State to keep track of starred routes.
     */
    const [starredRoutes, setStarredRoutes] = useState<string[]>([]);

    /**
     * State to handle any error messages.
     */
    const [error, setError] = useState<string | null>(null);

    /**
     * State to control the visibility of the equalizer modal.
     */
    const [showEqualizerModal, setShowEqualizerModal] = useState(false);

    /**
     * State to control the visibility of the edit modal.
     */
    const [showEditModal, setShowEditModal] = useState(false);

    /**
     * State to keep track of the currently selected item.
     */
    const [selectedItem, setSelectedItem] = useState<SelectedItemType>(null);

    /**
     * State to keep track of the type of the currently selected item.
     */
    const [selectedItemType, setSelectedItemType] = useState<'sources' | 'sinks' | 'routes' | 'group-sink' | 'group-source' | null>(null);

    /**
     * State to manage sorting configuration.
     */
    const [sortConfig, setSortConfig] = useState<SortConfig>({ key: '', direction: 'asc' });

    /**
     * Refs for section elements to enable smooth scrolling and highlighting.
     */
    const sectionRefs = {
        favoriteSources: useRef<HTMLDivElement>(null),
        favoriteSinks: useRef<HTMLDivElement>(null),
        favoriteRoutes: useRef<HTMLDivElement>(null),
        activeSources: useRef<HTMLDivElement>(null),
        activeSinks: useRef<HTMLDivElement>(null),
        activeRoutes: useRef<HTMLDivElement>(null),
    };

    /**
     * State to manage the expanded/collapsed state of sections.
     */
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

    /**
     * Effect to load starred items from local storage.
     */
    useEffect(() => {
      const starredSourcesData = JSON.parse(localStorage.getItem('starredSources') || '[]');
      const starredSinksData = JSON.parse(localStorage.getItem('starredSinks') || '[]');
      const starredRoutesData = JSON.parse(localStorage.getItem('starredRoutes') || '[]');
      setStarredSources(starredSourcesData);
      setStarredSinks(starredSinksData);
      setStarredRoutes(starredRoutesData);
    }, []);

    /**
     * Memoized actions object to handle various operations on sources, sinks, and routes.
     */
    const actions: Actions = useMemo(() => createActions(
      async () => Promise.resolve(), // No need to fetch data as it's handled by AppContext
      setError,
      (type, setter) => {
        if (type === 'sources') setStarredSources(setter);
        else if (type === 'sinks') setStarredSinks(setter);
        else if (type === 'routes') setStarredRoutes(setter);
      },
      setShowEqualizerModal,
      setSelectedItem,
      setSelectedItemType,
      openVNCModal, // Pass the function directly
      onToggleActiveSource, // Pass the function directly
      onListenToSink, // Pass the function directly
      onVisualizeSink, // Pass the function directly
      setShowEditModal, // Pass the function directly
      navigateToItem // Pass the function directly
    ), [onListenToSink, onVisualizeSink, onToggleActiveSource, openVNCModal, navigateToItem]);

    /**
     * Function to toggle the expanded/collapsed state of a section.
     */
    const toggleSection = (section: keyof typeof expandedSections) => {
      setExpandedSections((prev: typeof expandedSections) => {
        const newState = { ...prev, [section]: !prev[section] };
        localStorage.setItem('dashboardExpandedSections', JSON.stringify(newState));
        return newState;
      });
    };

    /**
     * Function to handle sorting of lists.
     */
    const onSort = (key: string) => {
      setSortConfig(prevConfig => ({
        key,
        direction: prevConfig.key === key && prevConfig.direction === 'asc' ? 'desc' : 'asc',
      }));
    };

    /**
     * Function to scroll to a specific section.
     */
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

    /**
     * Function to render the navigation bar.
     */
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

    /**
     * Function to render the edit modal.
     */
    const renderEditModal = () => {
      if (!showEditModal || !selectedItem || !selectedItemType) return null;

      const onClose = () => {
        setShowEditModal(false);
        setSelectedItem(null);
        setSelectedItemType(null);
      };

      switch (selectedItemType) {
        case 'sources':
          return <AddEditSource source={selectedItem as Source} onClose={onClose} onSave={() => {}} />;
        case 'sinks':
          return <AddEditSink sink={selectedItem as Sink} onClose={onClose} onSave={() => {}} />;
        case 'routes':
          return <AddEditRoute route={selectedItem as Route} onClose={onClose} onSave={() => {}} />;
        default:
          return null;
      }
    };

    /**
     * Main render function for the Dashboard component.
     */
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
              activeSource={contextActiveSource}
              actions={actions}
              sortConfig={sortConfig}
              onSort={onSort}
              hideExtraColumns={true}
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
              listeningToSink={listeningToSink?.name}
              visualizingSink={visualizingSink?.name}
              sortConfig={sortConfig}
              onSort={onSort}
              hideExtraColumns={true}
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
              activeSource={contextActiveSource}
              actions={actions}
              sortConfig={sortConfig}
              onSort={onSort}
              hideExtraColumns={true}
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
              listeningToSink={listeningToSink?.name}
              visualizingSink={visualizingSink?.name}
              sortConfig={sortConfig}
              onSort={onSort}
              hideExtraColumns={true}
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

        {showEqualizerModal && selectedItem && selectedItemType && (
          <div className="modal-overlay">
            <div className="modal-content">
              <ActionButton className="close-modal" onClick={() => setShowEqualizerModal(false)}>×</ActionButton>
              <Equalizer
                item={selectedItem}
                type={selectedItemType}
                onClose={() => setShowEqualizerModal(false)}
                onDataChange={() => {}}
              />
            </div>
          </div>
        )}

        {renderEditModal()}
      </div>
    );
};

export default Dashboard;
