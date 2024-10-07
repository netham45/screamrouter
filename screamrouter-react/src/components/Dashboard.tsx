import React, { useState, useEffect, useRef, useCallback } from 'react';
import { Link, useNavigate } from 'react-router-dom';
import ApiService, { Source, Sink, Route } from '../api/api';
import AddEditSource from './AddEditSource';
import AddEditSink from './AddEditSink';
import AddEditRoute from './AddEditRoute';
import VNC from './VNC';
import Equalizer from './Equalizer';
import '../styles/Dashboard.css';

const Dashboard: React.FC = () => {
  const navigate = useNavigate();
  const [sources, setSources] = useState<Source[]>([]);
  const [sinks, setSinks] = useState<Sink[]>([]);
  const [routes, setRoutes] = useState<Route[]>([]);
  const [starredSources, setStarredSources] = useState<string[]>([]);
  const [starredSinks, setStarredSinks] = useState<string[]>([]);
  const [starredRoutes, setStarredRoutes] = useState<string[]>([]);
  const [error, setError] = useState<string | null>(null);
  const svgRef = useRef<SVGSVGElement>(null);

  const [showEditModal, setShowEditModal] = useState(false);
  const [showVNCModal, setShowVNCModal] = useState(false);
  const [showEqualizerModal, setShowEqualizerModal] = useState(false);
  const [selectedItem, setSelectedItem] = useState<any>(null);
  const [selectedItemType, setSelectedItemType] = useState<'sources' | 'sinks' | 'routes' | null>(null);
  const [activeSource, setActiveSource] = useState<string | null>(null);

  useEffect(() => {
    fetchData();
    const starredSourcesData = JSON.parse(localStorage.getItem('starredSources') || '[]');
    const starredSinksData = JSON.parse(localStorage.getItem('starredSinks') || '[]');
    const starredRoutesData = JSON.parse(localStorage.getItem('starredRoutes') || '[]');
    setStarredSources(starredSourcesData);
    setStarredSinks(starredSinksData);
    setStarredRoutes(starredRoutesData);
    const active = localStorage.getItem('activeSource');
    setActiveSource(active);
  }, []);

  const fetchData = async () => {
    try {
      const [sourcesResponse, sinksResponse, routesResponse] = await Promise.all([
        ApiService.getSources(),
        ApiService.getSinks(),
        ApiService.getRoutes()
      ]);
      
      setSources(sourcesResponse.data);
      setSinks(sinksResponse.data);
      setRoutes(routesResponse.data.filter(route => route.enabled));
      setError(null);
    } catch (error) {
      console.error('Error fetching data:', error);
      setError('Failed to fetch data. Please try again later.');
    }
  };


  const renderConnections = useCallback(() => {
    if (!svgRef.current) return;

    const svg = svgRef.current;
    svg.innerHTML = '';

    routes.forEach((route) => {
      const sourceEl = document.getElementById(`source-${route.source}`);
      const sinkEl = document.getElementById(`sink-${route.sink}`);
      if (sourceEl && sinkEl) {
        const sourceRect = sourceEl.getBoundingClientRect();
        const sinkRect = sinkEl.getBoundingClientRect();
        const svgRect = svg.getBoundingClientRect();

        const x1 = sourceRect.right - svgRect.left;
        const y1 = sourceRect.top + sourceRect.height / 2 - svgRect.top;
        const x2 = sinkRect.left - svgRect.left;
        const y2 = sinkRect.top + sinkRect.height / 2 - svgRect.top;

        const path = document.createElementNS('http://www.w3.org/2000/svg', 'path');
        const midX = (x1 + x2) / 2;
        const controlY1 = y1 - 50;
        const controlY2 = y2 - 50;
        const d = `M ${x1} ${y1} C ${midX} ${controlY1}, ${midX} ${controlY2}, ${x2} ${y2}`;
        
        path.setAttribute('d', d);
        path.setAttribute('fill', 'none');
        path.setAttribute('stroke', 'green');
        path.setAttribute('stroke-width', '2');
        path.setAttribute('style', 'pointer-events: all; cursor: pointer;');
        path.classList.add('route-line', 'clickable');
        path.dataset.routeName = route.name;
        path.setAttribute('title', `Route: ${route.name}`);

        path.addEventListener('click', (e) => {
          e.preventDefault();
          console.log(`Clicked route: ${route.name}`);
          navigate(`/routes#route-${encodeURIComponent(route.name)}`);
        });

        path.addEventListener('mouseenter', () => {
          path.setAttribute('stroke', 'blue');
          path.setAttribute('stroke-width', '3');
        });

        path.addEventListener('mouseleave', () => {
          path.setAttribute('stroke', 'green');
          path.setAttribute('stroke-width', '2');
        });

        svg.appendChild(path);
      }
    });
  }, [routes, navigate]);

  useEffect(() => {
    window.addEventListener('resize', renderConnections);
    return () => {
      window.removeEventListener('resize', renderConnections);
    };
  }, [renderConnections]);

  useEffect(() => {
    renderConnections();
  }, [sources, sinks, renderConnections]);

  const activeSourceNames = new Set(routes.map(route => route.source));
  const activeSinkNames = new Set(routes.map(route => route.sink));

  const toggleStar = (type: 'sources' | 'sinks' | 'routes', name: string) => {
    let starredItems: string[];
    let setStarredItems: React.Dispatch<React.SetStateAction<string[]>>;

    switch (type) {
      case 'sources':
        starredItems = starredSources;
        setStarredItems = setStarredSources;
        break;
      case 'sinks':
        starredItems = starredSinks;
        setStarredItems = setStarredSinks;
        break;
      case 'routes':
        starredItems = starredRoutes;
        setStarredItems = setStarredRoutes;
        break;
    }

    const newStarredItems = starredItems.includes(name)
      ? starredItems.filter(item => item !== name)
      : [...starredItems, name];
    
    setStarredItems(newStarredItems);
    localStorage.setItem(`starred${type.charAt(0).toUpperCase() + type.slice(1)}`, JSON.stringify(newStarredItems));
  };

  const toggleEnabled = async (type: 'sources' | 'sinks' | 'routes', name: string, currentStatus: boolean) => {
    try {
      if (type === 'sources') {
        if (currentStatus) {
          await ApiService.disableSource(name);
        } else {
          await ApiService.enableSource(name);
        }
      } else if (type === 'sinks') {
        if (currentStatus) {
          await ApiService.disableSink(name);
        } else {
          await ApiService.enableSink(name);
        }
      } else if (type === 'routes') {
        if (currentStatus) {
          await ApiService.disableRoute(name);
        } else {
          await ApiService.enableRoute(name);
        }
      }
      await fetchData();
    } catch (error) {
      console.error(`Error toggling ${type} status:`, error);
      setError(`Failed to update ${type} status. Please try again.`);
    }
  };

  const updateVolume = async (type: 'sources' | 'sinks', name: string, volume: number) => {
    try {
      if (type === 'sources') {
        await ApiService.updateSourceVolume(name, volume);
      } else if (type === 'sinks') {
        await ApiService.updateSinkVolume(name, volume);
      }
      await fetchData();
    } catch (error) {
      console.error(`Error updating ${type} volume:`, error);
      setError(`Failed to update ${type} volume. Please try again.`);
    }
  };

  const handleGlobalHotkey = useCallback((event: KeyboardEvent) => {
    if (!activeSource) return;

    switch (event.key) {
      case 'MediaTrackPrevious':
        controlSource(activeSource, 'prevtrack');
        break;
      case 'MediaPlayPause':
        controlSource(activeSource, 'play');
        break;
      case 'MediaTrackNext':
        controlSource(activeSource, 'nexttrack');
        break;
    }
  }, [activeSource]);

  useEffect(() => {
    window.addEventListener('keydown', handleGlobalHotkey);
    return () => {
      window.removeEventListener('keydown', handleGlobalHotkey);
    };
  }, [handleGlobalHotkey]);

  const controlSource = async (sourceName: string, action: 'prevtrack' | 'play' | 'nexttrack') => {
    try {
      await fetch(`https://screamrouter.netham45.org/sources/${sourceName}/${action}`, { method: 'GET' });
      // Optionally update the UI or refetch data if needed
    } catch (error) {
      console.error(`Error controlling source: ${error}`);
      setError(`Failed to control source. Please try again.`);
    }
  };

  const toggleActive = (name: string) => {
    const newActiveSource = activeSource === name ? null : name;
    setActiveSource(newActiveSource);
    localStorage.setItem('activeSource', newActiveSource || '');
  };

  const renderFavoriteSection = (title: string, items: any[], type: 'sources' | 'sinks' | 'routes') => {
    const starredItems = type === 'sources' ? starredSources : type === 'sinks' ? starredSinks : starredRoutes;
    const filteredItems = items.filter(item => starredItems.includes(item.name));

    return (
      <div className="favorite-section">
        <h3>{title}</h3>
        <table>
          <thead>
            <tr>
              <th>Name</th>
              <th>Status</th>
              <th>Tools</th>
            </tr>
          </thead>
          <tbody>
            {filteredItems.map(item => (
              <tr key={item.name}>
                <td>
                  <Link to={`/${type}#${type.slice(0, -1)}-${encodeURIComponent(item.name)}`}>
                    {item.name}
                  </Link>
                </td>
                <td>
                  <button 
                    onClick={() => toggleEnabled(type, item.name, item.enabled)}
                    className={item.enabled ? 'enabled' : 'disabled'}
                  >
                    {item.enabled ? 'Enabled' : 'Disabled'}
                  </button>
                </td>
                <td>
                  <button onClick={() => {
                    setSelectedItem(item);
                    setSelectedItemType(type);
                    setShowEditModal(true);
                  }}>Edit</button>
                  <button onClick={() => toggleStar(type, item.name)}>
                    {starredItems.includes(item.name) ? '★' : '☆'}
                  </button>
                  {type === 'sources' && (
                    <>
                      <button 
                        onClick={() => toggleActive(item.name)}
                        className={activeSource === item.name ? 'active' : ''}
                      >
                        {activeSource === item.name ? '⬤' : '◯'}
                      </button>
                      {item.vnc_ip && item.vnc_port && (
                        <>
                          <button onClick={() => {
                            setSelectedItem(item);
                            setShowVNCModal(true);
                          }}>VNC</button>
                          <button onClick={() => controlSource(item.name, 'prevtrack')}>⏮</button>
                          <button onClick={() => controlSource(item.name, 'play')}>⏯</button>
                          <button onClick={() => controlSource(item.name, 'nexttrack')}>⏭</button>
                        </>
                      )}
                    </>
                  )}
                  <button onClick={() => {
                    setSelectedItem(item);
                    setSelectedItemType(type);
                    setShowEqualizerModal(true);
                  }}>EQ</button>
                  <input
                    type="range"
                    min="0"
                    max="1"
                    step="0.01"
                    value={item.volume}
                    onChange={(e) => updateVolume(type as 'sources' | 'sinks', item.name, parseFloat(e.target.value))}
                  />
                </td>
              </tr>
            ))}
          </tbody>
        </table>
      </div>
    );
  };

  const renderActiveSourceSection = () => {
    const activeSourceItem = sources.find(source => source.name === activeSource);
    if (!activeSourceItem) return null;

    return (
      <div className="active-source-section">
        <h3>Active Source</h3>
        <table>
          <thead>
            <tr>
              <th>Name</th>
              <th>Status</th>
              <th>Tools</th>
            </tr>
          </thead>
          <tbody>
            <tr>
              <td>
                <Link to={`/sources#source-${encodeURIComponent(activeSourceItem.name)}`}>
                  {activeSourceItem.name}
                </Link>
              </td>
              <td>
                <button 
                  onClick={() => toggleEnabled('sources', activeSourceItem.name, activeSourceItem.enabled)}
                  className={activeSourceItem.enabled ? 'enabled' : 'disabled'}
                >
                  {activeSourceItem.enabled ? 'Enabled' : 'Disabled'}
                </button>
              </td>
              <td>
                <button onClick={() => {
                  setSelectedItem(activeSourceItem);
                  setSelectedItemType('sources');
                  setShowEditModal(true);
                }}>Edit</button>
                <button onClick={() => toggleStar('sources', activeSourceItem.name)}>
                  {starredSources.includes(activeSourceItem.name) ? '★' : '☆'}
                </button>
                <button 
                  onClick={() => toggleActive(activeSourceItem.name)}
                  className="active"
                >
                  ⬤
                </button>
                {activeSourceItem.vnc_ip && activeSourceItem.vnc_port && (
                  <>
                    <button onClick={() => {
                      setSelectedItem(activeSourceItem);
                      setShowVNCModal(true);
                    }}>VNC</button>
                    <button onClick={() => controlSource(activeSourceItem.name, 'prevtrack')}>⏮</button>
                    <button onClick={() => controlSource(activeSourceItem.name, 'play')}>⏯</button>
                    <button onClick={() => controlSource(activeSourceItem.name, 'nexttrack')}>⏭</button>
                  </>
                )}
                <button onClick={() => {
                  setSelectedItem(activeSourceItem);
                  setSelectedItemType('sources');
                  setShowEqualizerModal(true);
                }}>EQ</button>
                <input
                  type="range"
                  min="0"
                  max="1"
                  step="0.01"
                  value={activeSourceItem.volume}
                  onChange={(e) => updateVolume('sources', activeSourceItem.name, parseFloat(e.target.value))}
                />
              </td>
            </tr>
          </tbody>
        </table>
      </div>
    );
  };

  return (
    <div className="dashboard">
            <h1>Dashboard</h1>
      {error && <div className="error-message">{error}</div>}
      {renderActiveSourceSection()}
      <div className="favorite-sections">
        {renderFavoriteSection('Favorite Sources', sources, 'sources')}
        {renderFavoriteSection('Favorite Sinks', sinks, 'sinks')}
        {renderFavoriteSection('Favorite Routes', routes, 'routes')}
      </div>
      <div className="dashboard-content">
        <div className="sources-column">
          <h2>Sources</h2>
          {sources.filter(source => source.enabled && activeSourceNames.has(source.name)).map(source => (
            <Link
              key={source.name}
              to={`/sources#source-${encodeURIComponent(source.name)}`}
              id={`source-${source.name}`}
              className="dashboard-item enabled"
            >
              {source.name}
            </Link>
          ))}
        </div>
        <div className="sinks-column">
          <h2>Sinks</h2>
          {sinks.filter(sink => sink.enabled && activeSinkNames.has(sink.name)).map(sink => (
            <Link
              key={sink.name}
              to={`/sinks#sink-${encodeURIComponent(sink.name)}`}
              id={`sink-${sink.name}`}
              className="dashboard-item enabled"
            >
              {sink.name}
            </Link>
          ))}
        </div>
      </div>
      <svg ref={svgRef} className="connections" style={{ pointerEvents: 'none', zIndex: 1 }}></svg>
      {showEditModal && selectedItem && selectedItemType && (
        <div className="modal-overlay">
          <div className="modal-content">
            <button className="close-modal" onClick={() => setShowEditModal(false)}>×</button>
            {selectedItemType === 'sources' && (
              <AddEditSource
                source={selectedItem}
                onClose={() => setShowEditModal(false)}
                onSubmit={() => { fetchData(); setShowEditModal(false); }}
              />
            )}
            {selectedItemType === 'sinks' && (
              <AddEditSink
                sink={selectedItem}
                onClose={() => setShowEditModal(false)}
                onSubmit={() => { fetchData(); setShowEditModal(false); }}
              />
            )}
            {selectedItemType === 'routes' && (
              <AddEditRoute
                route={selectedItem}
                onClose={() => setShowEditModal(false)}
                onSubmit={() => { fetchData(); setShowEditModal(false); }}
              />
            )}
          </div>
        </div>
      )}

      {showVNCModal && selectedItem && (
        <div className="modal-overlay">
          <div className="modal-content">
            <button className="close-modal" onClick={() => setShowVNCModal(false)}>×</button>
            <VNC source={selectedItem} />
          </div>
        </div>
      )}

      {showEqualizerModal && selectedItem && selectedItemType && (
        <div className="modal-overlay">
          <div className="modal-content">
            <button className="close-modal" onClick={() => setShowEqualizerModal(false)}>×</button>
            <Equalizer
              item={selectedItem}
              type={selectedItemType === 'routes' ? 'routes' : selectedItemType}
              onClose={() => setShowEqualizerModal(false)}
            />
          </div>
        </div>
      )}
    </div>
  );
};

export default Dashboard;