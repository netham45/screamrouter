import React from 'react';
import { Sink, Route } from '../api/api';
import SinkItem from './SinkItem';
import { useAppContext } from '../context/AppContext';
import { SortConfig } from '../utils/commonUtils';

interface SinkListProps {
  sinks: Sink[];
  routes: Route[];
  starredSinks: string[];
  onToggleSink: (name: string) => void;
  onDeleteSink: (name: string) => void;
  onUpdateVolume: (name: string, volume: number) => void;
  onToggleStar: (name: string) => void;
  onEditSink: (sink: Sink) => void;
  onShowEqualizer: (sink: Sink) => void;
  sinkRefs: React.MutableRefObject<{[key: string]: HTMLTableRowElement}>;
  onDragStart: (e: React.DragEvent<HTMLSpanElement>, index: number) => void;
  onDragEnter: (e: React.DragEvent<HTMLTableRowElement>, index: number) => void;
  onDragLeave: (e: React.DragEvent<HTMLTableRowElement>) => void;
  onDragOver: (e: React.DragEvent<HTMLTableRowElement>) => void;
  onDrop: (e: React.DragEvent<HTMLTableRowElement>, index: number) => void;
  onDragEnd: (e: React.DragEvent<HTMLSpanElement>) => void;
  jumpToAnchor: (name: string) => void;
  getActiveRoutes: (sinkName: string) => Route[];
  getDisabledRoutes: (sinkName: string) => Route[];
  expandedRoutes: string[];
  toggleExpandRoutes: (name: string) => void;
  sortConfig: SortConfig;
  onSort: (key: string) => void;
  listeningToSink: Sink | null;
  visualizingSink: Sink | null;
  onListenToSink: (sink: Sink | null) => void;
  onVisualizeSink: (sink: Sink | null) => void;
}

const SinkList: React.FC<SinkListProps> = ({
  sinks,
  routes,
  starredSinks,
  onToggleSink,
  onDeleteSink,
  onUpdateVolume,
  onToggleStar,
  onEditSink,
  onShowEqualizer,
  sinkRefs,
  onDragStart,
  onDragEnter,
  onDragLeave,
  onDragOver,
  onDrop,
  onDragEnd,
  jumpToAnchor,
  getActiveRoutes,
  getDisabledRoutes,
  expandedRoutes,
  toggleExpandRoutes,
  sortConfig,
  onSort,
  listeningToSink,
  visualizingSink,
  onListenToSink,
  onVisualizeSink
}) => {
  const renderSortIcon = (key: string) => {
    if (sortConfig.key === key) {
      return sortConfig.direction === 'asc' ? ' ▲' : ' ▼';
    }
    return null;
  };

  // Wrap onListenToSink to return a Promise
  const handleListenToSink = async (sink: Sink | null) => {
    await Promise.resolve(onListenToSink(sink));
  };

  return (
    <table className="sinks-table">
      <thead>
        <tr>
          <th>Reorder</th>
          <th onClick={() => onSort('favorite')}>Favorite{renderSortIcon('favorite')}</th>
          <th onClick={() => onSort('name')}>Name{renderSortIcon('name')}</th>
          <th onClick={() => onSort('ip')}>IP Address{renderSortIcon('ip')}</th>
          <th onClick={() => onSort('port')}>Port{renderSortIcon('port')}</th>
          <th onClick={() => onSort('enabled')}>Status{renderSortIcon('enabled')}</th>
          <th onClick={() => onSort('volume')}>Volume{renderSortIcon('volume')}</th>
          <th>Actions</th>
        </tr>
      </thead>
      <tbody>
        {sinks.map((sink, index) => (
          <SinkItem
            key={sink.name}
            sink={sink}
            index={index}
            isStarred={starredSinks.includes(sink.name)}
            onToggleSink={onToggleSink}
            onDeleteSink={onDeleteSink}
            onUpdateVolume={onUpdateVolume}
            onToggleStar={onToggleStar}
            onEditSink={onEditSink}
            onShowEqualizer={onShowEqualizer}
            onListenToSink={handleListenToSink}
            onVisualizeSink={onVisualizeSink}
            isListening={listeningToSink?.name === sink.name}
            isVisualizing={visualizingSink?.name === sink.name}
            sinkRefs={sinkRefs}
            onDragStart={onDragStart}
            onDragEnter={onDragEnter}
            onDragLeave={onDragLeave}
            onDragOver={onDragOver}
            onDrop={onDrop}
            onDragEnd={onDragEnd}
            jumpToAnchor={jumpToAnchor}
            activeRoutes={getActiveRoutes(sink.name)}
            disabledRoutes={getDisabledRoutes(sink.name)}
            isExpanded={expandedRoutes.includes(sink.name)}
            toggleExpandRoutes={toggleExpandRoutes}
          />
        ))}
      </tbody>
    </table>
  );
};

export default SinkList;