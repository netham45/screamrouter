import React from 'react';
import { Source, Route } from '../api/api';
import { renderLinkWithAnchor, ActionButton, VolumeSlider } from '../utils/commonUtils';

interface ActiveSourceSectionProps {
  activeSourceItem: Source | null;
  toggleEnabled: (type: 'sources', name: string, enabled: boolean) => void;
  toggleStar: (type: 'sources', name: string) => void;
  toggleActive: (name: string) => void;
  starredSources: string[];
  setSelectedItem: (item: Source) => void;
  setSelectedItemType: (type: 'sources') => void;
  setShowEditModal: (show: boolean) => void;
  setShowVNCModal: (show: boolean) => void;
  controlSource: (sourceName: string, action: 'prevtrack' | 'play' | 'nexttrack') => void;
  setShowEqualizerModal: (show: boolean) => void;
  updateVolume: (type: 'sources', name: string, volume: number) => void;
  getRoutesForSource: (sourceName: string) => Route[];
  jumpToAnchor: (name: string) => void;
}

const ActiveSourceSection: React.FC<ActiveSourceSectionProps> = ({
  activeSourceItem,
  toggleEnabled,
  toggleStar,
  toggleActive,
  starredSources,
  setSelectedItem,
  setSelectedItemType,
  setShowEditModal,
  setShowVNCModal,
  controlSource,
  setShowEqualizerModal,
  updateVolume,
  getRoutesForSource,
  jumpToAnchor
}) => {
  if (!activeSourceItem) {
    return null; // or return a message like "No active source selected"
  }

  const renderRouteLinks = (routes: Route[]) => {
    if (routes.length === 0) return 'None';
    return routes.map((route, index) => (
      <React.Fragment key={route.name}>
        {index > 0 && ', '}
        {renderLinkWithAnchor('/sinks', route.sink, 'fa-volume-up')}
      </React.Fragment>
    ));
  };

  const renderGroupMembers = () => {
    if (!activeSourceItem.is_group || !activeSourceItem.group_members) return null;
    return (
      <div className="group-members">
        <span>Group members: </span>
        {activeSourceItem.group_members.map((member, index) => (
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

  return (
    <div className="active-source-section">
      <h2><i className="fas fa-broadcast-tower"></i> Active Source</h2>
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
              {renderLinkWithAnchor('/sources', activeSourceItem.name, 'fa-music')}
            </td>
            <td>
              <div className="subtext">
                <div>Routes to: {renderRouteLinks(getRoutesForSource(activeSourceItem.name))}</div>
                {renderGroupMembers()}
              </div>
            </td>
            <td>
              <ActionButton 
                onClick={() => toggleEnabled('sources', activeSourceItem.name, activeSourceItem.enabled)}
                className={activeSourceItem.enabled ? 'enabled' : 'disabled'}
              >
                {activeSourceItem.enabled ? 'Enabled' : 'Disabled'}
              </ActionButton>
            </td>
            <td>
              {(activeSourceItem.vnc_ip || activeSourceItem.vnc_port) && (
                <>
                  <ActionButton onClick={() => {
                    setSelectedItem(activeSourceItem);
                    setShowVNCModal(true);
                  }}>
                    VNC
                  </ActionButton>
                  <ActionButton onClick={() => controlSource(activeSourceItem.name, 'prevtrack')}>
                    ⏮
                  </ActionButton>
                  <ActionButton onClick={() => controlSource(activeSourceItem.name, 'play')}>
                    ⏯
                  </ActionButton>
                  <ActionButton onClick={() => controlSource(activeSourceItem.name, 'nexttrack')}>
                    ⏭
                  </ActionButton>
                </>
              )}
              <ActionButton onClick={() => {
                setSelectedItem(activeSourceItem);
                setSelectedItemType('sources');
                setShowEqualizerModal(true);
              }}>
                Equalizer
              </ActionButton>
              <VolumeSlider
                value={activeSourceItem.volume}
                onChange={(value) => updateVolume('sources', activeSourceItem.name, value)}
              />
            </td>
          </tr>
        </tbody>
      </table>
    </div>
  );
};

export default ActiveSourceSection;