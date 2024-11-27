/**
 * React component for displaying a section of favorite items (sources, sinks, or routes).
 * This component filters the provided items to only include those that are starred and renders them in a table format.
 * Each item includes controls for interaction based on its type.
 *
 * @param {React.FC} props - The properties for the component.
 * @param {React.ReactNode} props.title - The title of the section.
 * @param {FavoriteItem[]} props.items - The list of items to display.
 * @param {'sources' | 'sinks' | 'routes' | 'group-sink' | 'group-source'} props.type - The type of items ('sources', 'sinks', 'routes', 'group-sink', 'group-source').
 * @param {string[]} props.starredItems - The list of names of starred items.
 * @param {(type: 'sources' | 'sinks' | 'routes' | 'group-sink' | 'group-source', name: string, enabled: boolean) => void} props.toggleEnabled - Function to toggle the enabled state of an item.
 * @param {(type: 'sources' | 'sinks' | 'routes' | 'group-sink' | 'group-source', name: string) => void} props.toggleStar - Function to toggle the starred state of an item.
 * @param {(item: FavoriteItem) => void} props.setSelectedItem - Function to set the selected item.
 * @param {(type: 'sources' | 'sinks' | 'routes' | 'group-sink' | 'group-source') => void} props.setSelectedItemType - Function to set the type of the selected item.
 * @param {(show: boolean) => void} props.setShowEditModal - Function to show or hide the edit modal.
 * @param {(name: string) => void} [props.toggleActive] - Optional function to toggle the active state of a source.
 * @param {string | null} [props.activeSource] - Optional name of the currently active source.
 * @param {(sourceName: string, action: 'prevtrack' | 'play' | 'nexttrack') => void} [props.controlSource] - Optional function to control a source (e.g., play, pause).
 * @param {(show: boolean) => void} [props.setShowVNCModal] - Optional function to show or hide the VNC modal.
 * @param {(type: 'sources' | 'sinks' | 'routes' | 'group-sink' | 'group-source', name: string, volume: number) => void} props.updateVolume - Function to update the volume of an item.
 * @param {(item: FavoriteItem, type: 'sources' | 'sinks' | 'routes' | 'group-sink' | 'group-source') => React.ReactNode} props.renderControls - Function to render controls for an item.
 * @param {(sourceName: string) => Route[]} props.getRoutesForSource - Function to get routes for a source.
 * @param {(sinkName: string) => Route[]} props.getRoutesForSink - Function to get routes for a sink.
 * @param {(name: string, type: 'sources' | 'sinks') => string} props.getGroupMembers - Function to get group members of an item.
 * @param {(name: string) => void} props.jumpToAnchor - Function to jump to an anchor in the document.
 */
import React from 'react';
import { Source, Sink, Route } from '../api/api';
import { renderLinkWithAnchor } from '../utils/commonUtils';

type FavoriteItem = Source | Sink | Route;

interface FavoriteSectionProps {
  title: React.ReactNode;
  items: FavoriteItem[];
  type: 'sources' | 'sinks' | 'routes' | 'group-sink' | 'group-source';
  starredItems: string[];
  toggleEnabled: (type: 'sources' | 'sinks' | 'routes' | 'group-sink' | 'group-source', name: string, enabled: boolean) => void;
  toggleStar: (type: 'sources' | 'sinks' | 'routes' | 'group-sink' | 'group-source', name: string) => void;
  setSelectedItem: (item: FavoriteItem) => void;
  setSelectedItemType: (type: 'sources' | 'sinks' | 'routes' | 'group-sink' | 'group-source') => void;
  setShowEditModal: (show: boolean) => void;
  toggleActive?: (name: string) => void;
  activeSource?: string | null;
  controlSource?: (sourceName: string, action: 'prevtrack' | 'play' | 'nexttrack') => void;
  setShowVNCModal?: (show: boolean) => void;
  updateVolume: (type: 'sources' | 'sinks' | 'routes' | 'group-sink' | 'group-source', name: string, volume: number) => void;
  renderControls: (item: FavoriteItem, type: 'sources' | 'sinks' | 'routes' | 'group-sink' | 'group-source') => React.ReactNode;
  getRoutesForSource: (sourceName: string) => Route[];
  getRoutesForSink: (sinkName: string) => Route[];
  getGroupMembers: (name: string, type: 'sources' | 'sinks') => string;
  jumpToAnchor: (name: string) => void;
}

/**
 * React functional component for the FavoriteSection.
 *
 * @param {FavoriteSectionProps} props - The properties for the component.
 */
const FavoriteSection: React.FC<FavoriteSectionProps> = ({
  title,
  items,
  type,
  starredItems,
  renderControls,
  getRoutesForSource,
  getRoutesForSink,
  jumpToAnchor
}) => {
  /**
   * Filters the items to only include those that are starred.
   */
  const filteredItems = items.filter(item => starredItems.includes(item.name));

  /**
   * Returns the icon class based on the item type.
   *
   * @param {'sources' | 'sinks' | 'routes' | 'group-sink' | 'group-source'} itemType - The type of the item.
   * @returns {string} The icon class name.
   */
  const getIconClass = (itemType: 'sources' | 'sinks' | 'routes' | 'group-sink' | 'group-source') => {
    switch (itemType) {
      case 'sources':
        return 'fa-music';
      case 'sinks':
        return 'fa-volume-up';
      case 'routes':
        return 'fa-route';
      default:
        return '';
    }
  };

  /**
   * Renders links for routes.
   *
   * @param {Route[]} routes - The list of routes.
   * @param {'to' | 'from'} routeType - The type of the route ('to' or 'from').
   * @returns {React.ReactNode} The rendered route links.
   */
  const renderRouteLinks = (routes: Route[], routeType: 'to' | 'from') => {
    if (routes.length === 0) return 'None';
    return routes.map((route, index) => (
      <React.Fragment key={route.name}>
        {index > 0 && ', '}
        {renderLinkWithAnchor(
          routeType === 'to' ? '/sinks' : '/sources',
          routeType === 'to' ? route.sink : route.source,
          routeType === 'to' ? 'fa-volume-up' : 'fa-music'
        )}
      </React.Fragment>
    ));
  };

  /**
   * Renders group members for a source or sink.
   *
   * @param {Source | Sink} item - The source or sink item.
   * @returns {React.ReactNode} The rendered group members.
   */
  const renderGroupMembers = (item: Source | Sink) => {
    if (!('is_group' in item) || !item.is_group || !item.group_members) return null;
    return (
      <div className="group-members">
        <span>Group members: </span>
        {item.group_members.map((member, index) => (
          <React.Fragment key={member}>
            {index > 0 && ', '}
            <a href={`#${type.slice(0, -1)}-${encodeURIComponent(member)}`} onClick={(e) => { e.preventDefault(); jumpToAnchor(member); }}>
              {member}
            </a>
          </React.Fragment>
        ))}
      </div>
    );
  };

  /**
   * Renders the FavoriteSection component.
   *
   * @returns {JSX.Element} The rendered JSX element.
   */
  return (
    <div className="favorite-section">
      <h2>{title}</h2>
      <table>
        <thead>
          <tr>
            <th>Name</th>
            <th>Controls</th>
          </tr>
        </thead>
        <tbody>
          {filteredItems.map(item => (
            <tr key={item.name}>
              <td>
                {renderLinkWithAnchor(`/${type}`, item.name, getIconClass(type))}
                <div className="subtext">
                  {type !== 'routes' && (
                    <>
                      <div>Routes {type === 'sources' ? 'to' : 'from'}: {renderRouteLinks(type === 'sources' ? getRoutesForSource(item.name) : getRoutesForSink(item.name), type === 'sources' ? 'to' : 'from')}</div>
                      {renderGroupMembers(item as Source | Sink)}
                    </>
                  )}
                  {type === 'routes' && (
                    <>
                      <div>Source: {renderLinkWithAnchor('/sources', (item as Route).source, 'fa-music')}</div>
                      <div>Sink: {renderLinkWithAnchor('/sinks', (item as Route).sink, 'fa-volume-up')}</div>
                    </>
                  )}
                </div>
              </td>
              <td>
                {renderControls(item, type)}
              </td>
            </tr>
          ))}
        </tbody>
      </table>
    </div>
  );
};

export default FavoriteSection;
