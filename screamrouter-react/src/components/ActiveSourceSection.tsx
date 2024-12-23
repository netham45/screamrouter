/**
 * This file contains the ActiveSourceSection component, which displays and controls the primary audio source.
 * It includes functionality for enabling/disabling the source, adjusting volume, opening equalizer and VNC modals,
 * controlling playback (prev track, play, next track), and toggling star status.
 */

import React from 'react';
import { Source, Route } from '../api/api';
import ActionButton from './controls/ActionButton';
import VolumeSlider from './controls/VolumeSlider';
import { renderLinkWithAnchor } from '../utils/commonUtils';

/**
 * Interface defining the props for the PrimarySourceSection component.
 */
interface PrimarySourceSectionProps {
  primarySourceItem: Source | null;
  toggleEnabled: (type: 'sources', name: string, enabled: boolean) => void;
  toggleStar: (type: 'sources', name: string) => void;
  starredSources: string[];
  setSelectedItem: (item: Source) => void;
  setSelectedItemType: (type: 'sources') => void;
  setShowVNCModal: (show: boolean) => void;
  controlSource: (sourceName: string, action: 'prevtrack' | 'play' | 'nexttrack') => void;
  setShowEqualizerModal: (show: boolean) => void;
  updateVolume: (type: 'sources', name: string, volume: number) => void;
  getRoutesForSource: (sourceName: string) => Route[];
  jumpToAnchor: (name: string) => void;
  onDataChange: () => void;
}

/**
 * React functional component that displays and controls the primary audio source.
 * @param {PrimarySourceSectionProps} props - Component properties including the primary source item, control functions, and state management functions.
 * @returns {JSX.Element | null} Rendered PrimarySourceSection component or null if no primary source is available.
 */
const PrimarySourceSection: React.FC<PrimarySourceSectionProps> = ({
  primarySourceItem,
  toggleEnabled,
  toggleStar,
  starredSources,
  setSelectedItem,
  setSelectedItemType,
  setShowVNCModal,
  controlSource,
  setShowEqualizerModal,
  updateVolume,
  getRoutesForSource,
  jumpToAnchor,
  onDataChange
}) => {
  if (!primarySourceItem) {
    return null;
  }

  /**
   * Renders links to routes associated with the primary source.
   * @param {Route[]} routes - Array of route objects.
   * @returns {JSX.Element} Rendered route links or "None" if no routes are present.
   */
  const renderRouteLinks = (routes: Route[]) => {
    if (routes.length === 0) return 'None';
    return routes.map((route, index) => (
      <React.Fragment key={route.name}>
        {index > 0 && ', '}
        {renderLinkWithAnchor('/sinks', route.sink, 'fa-volume-up')}
      </React.Fragment>
    ));
  };

  /**
   * Renders links to group members if the primary source is part of a group.
   * @returns {JSX.Element | null} Rendered group member links or null if not part of a group.
   */
  const renderGroupMembers = () => {
    if (!primarySourceItem.is_group || !primarySourceItem.group_members) return null;
    return (
      <div className="group-members">
        <span>Group members: </span>
        {primarySourceItem.group_members.map((member, index) => (
          <React.Fragment key={member}>
            {index > 0 && ', '}
            <a href={`#source-${encodeURIComponent(member)}`} onClick={(e) => { e.preventDefault(); jumpToAnchor(member); }}>
              {member}
            </a>
          </React.Fragment>
        ))}
      </div>
    );
  };

  /**
   * Renders control elements for the primary source.
   * @returns {JSX.Element} Rendered control elements.
   */
  const renderControls = () => {
    console.log('Rendering controls for primary source:', primarySourceItem); // Debug log
    return (
      <>
        <ActionButton onClick={() => toggleEnabled('sources', primarySourceItem.name, primarySourceItem.enabled)}
          className={primarySourceItem.enabled ? 'enabled' : 'disabled'}
        >
          {primarySourceItem.enabled ? 'Disable' : 'Enable'}
        </ActionButton>
        <VolumeSlider
          value={primarySourceItem.volume}
          onChange={(value) => updateVolume('sources', primarySourceItem.name, value)}
        />
        <ActionButton onClick={() => {
          console.log('Equalizer button clicked for primary source:', primarySourceItem.name); // Debug log
          setSelectedItem(primarySourceItem);
          setSelectedItemType('sources');
          setShowEqualizerModal(true);
          onDataChange();
        }}>
          Equalizer
        </ActionButton>
        {('vnc_ip' in primarySourceItem || 'vnc_port' in primarySourceItem) && (
          <>
            <ActionButton onClick={() => {
              console.log('VNC button clicked for primary source:', primarySourceItem.name); // Debug log
              setSelectedItem(primarySourceItem);
              setShowVNCModal(true);
            }}>
              VNC
            </ActionButton>
            <ActionButton onClick={() => controlSource(primarySourceItem.name, 'prevtrack')}>⏮</ActionButton>
            <ActionButton onClick={() => controlSource(primarySourceItem.name, 'play')}>⏯</ActionButton>
            <ActionButton onClick={() => controlSource(primarySourceItem.name, 'nexttrack')}>⏭</ActionButton>
          </>
        )}
        <ActionButton onClick={() => toggleStar('sources', primarySourceItem.name)}>
          {starredSources?.includes(primarySourceItem.name) ? '★' : '☆'}
        </ActionButton>
      </>
    );
  };

  /**
   * Main render method for the PrimarySourceSection component.
   * @returns {JSX.Element} Rendered PrimarySourceSection component structure.
   */
  return (
    <div className="primary-source-section">
      <h2><i className="fas fa-broadcast-tower"></i> Primary Source</h2>
      <table>
        <thead>
          <tr>
            <th>Name</th>
            <th>Details</th>
            <th>Status</th>
            <th>Controls</th>
          </tr>
        </thead>
        <tbody>
          <tr>
            <td>
              {renderLinkWithAnchor('/sources', primarySourceItem.name, 'fa-music')}
            </td>
            <td>
              <div className="subtext">
                <div>Routes to: {renderRouteLinks(getRoutesForSource(primarySourceItem.name))}</div>
                {renderGroupMembers()}
              </div>
            </td>
            <td>
              {primarySourceItem.enabled ? 'Enabled' : 'Disabled'}
            </td>
            <td>
              {renderControls()}
            </td>
          </tr>
        </tbody>
      </table>
    </div>
  );
};

/**
 * Exports the PrimarySourceSection component as the default export.
 */
export default PrimarySourceSection;
