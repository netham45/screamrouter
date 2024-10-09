import React from 'react';
import { Source, Route } from '../api/api';
import SourceItem from './SourceItem';
import { SortConfig } from '../utils/commonUtils';

interface SourceListProps {
  sources: Source[];
  routes: Route[];
  starredSources: string[];
  activeSource: string | null;
  onToggleSource: (name: string) => void;
  onDeleteSource: (name: string) => void;
  onUpdateVolume: (name: string, volume: number) => void;
  onToggleStar: (name: string) => void;
  onToggleActiveSource: (name: string) => void;
  onEditSource: (source: Source) => void;
  onShowEqualizer: (source: Source) => void;
  onShowVNC: (source: Source) => void;
  onControlSource: (sourceName: string, action: 'prevtrack' | 'play' | 'nexttrack') => void;
  sourceRefs: React.MutableRefObject<{[key: string]: HTMLTableRowElement}>;
  onDragStart: (e: React.DragEvent<HTMLSpanElement>, index: number) => void;
  onDragEnter: (e: React.DragEvent<HTMLTableRowElement>, index: number) => void;
  onDragLeave: (e: React.DragEvent<HTMLTableRowElement>) => void;
  onDragOver: (e: React.DragEvent<HTMLTableRowElement>) => void;
  onDrop: (e: React.DragEvent<HTMLTableRowElement>, index: number) => void;
  onDragEnd: (e: React.DragEvent<HTMLSpanElement>) => void;
  jumpToAnchor: (name: string) => void;
  getActiveRoutes: (sourceName: string) => Route[];
  getDisabledRoutes: (sourceName: string) => Route[];
  expandedRoutes: string[];
  toggleExpandRoutes: (name: string) => void;
  sortConfig: SortConfig;
  onSort: (key: string) => void;
}

const SourceList: React.FC<SourceListProps> = ({
  sources,
  routes,
  starredSources,
  activeSource,
  onToggleSource,
  onDeleteSource,
  onUpdateVolume,
  onToggleStar,
  onToggleActiveSource,
  onEditSource,
  onShowEqualizer,
  onShowVNC,
  onControlSource,
  sourceRefs,
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
  onSort
}) => {
  const renderSortIcon = (key: string) => {
    if (sortConfig.key === key) {
      return sortConfig.direction === 'asc' ? ' ▲' : ' ▼';
    }
    return null;
  };

  return (
    <table className="sources-table">
      <thead>
        <tr>
          <th>Reorder</th>
          <th onClick={() => onSort('favorite')}>Favorite{renderSortIcon('favorite')}</th>
          <th onClick={() => onSort('active')}>Active{renderSortIcon('active')}</th>
          <th onClick={() => onSort('name')}>Name{renderSortIcon('name')}</th>
          <th onClick={() => onSort('ip')}>IP Address{renderSortIcon('ip')}</th>
          <th onClick={() => onSort('enabled')}>Status{renderSortIcon('enabled')}</th>
          <th onClick={() => onSort('volume')}>Volume{renderSortIcon('volume')}</th>
          <th>Actions</th>
        </tr>
      </thead>
      <tbody>
        {sources.map((source, index) => (
          <SourceItem
            key={source.name}
            source={source}
            index={index}
            isStarred={starredSources.includes(source.name)}
            isActive={activeSource === source.name}
            onToggleSource={() => onToggleSource(source.name)}
            onDeleteSource={onDeleteSource}
            onUpdateVolume={(volume) => onUpdateVolume(source.name, volume)}
            onToggleStar={onToggleStar}
            onToggleActiveSource={onToggleActiveSource}
            onEditSource={onEditSource}
            onShowEqualizer={onShowEqualizer}
            onShowVNC={onShowVNC}
            onControlSource={onControlSource}
            sourceRefs={sourceRefs}
            onDragStart={onDragStart}
            onDragEnter={onDragEnter}
            onDragLeave={onDragLeave}
            onDragOver={onDragOver}
            onDrop={onDrop}
            onDragEnd={onDragEnd}
            jumpToAnchor={jumpToAnchor}
            activeRoutes={getActiveRoutes(source.name)}
            disabledRoutes={getDisabledRoutes(source.name)}
            isExpanded={expandedRoutes.includes(source.name)}
            toggleExpandRoutes={toggleExpandRoutes}
          />
        ))}
      </tbody>
    </table>
  );
};

export default SourceList;