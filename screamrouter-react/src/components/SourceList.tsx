import React, { useRef } from 'react';
import { Source, Route } from '../api/api';
import SourceItem from './SourceItem';
import { Actions } from '../utils/actions';
import { SortConfig } from '../utils/commonUtils';

interface SourceListProps {
  sources: Source[];
  routes: Route[];
  starredSources: string[];
  activeSource: string | null;
  actions: Actions;
  sortConfig: SortConfig;
  onSort: (key: string) => void;
}

const SourceList: React.FC<SourceListProps> = ({
  sources,
  routes,
  starredSources,
  activeSource,
  actions,
  sortConfig,
  onSort,
}) => {
  const sourceRefs = useRef<{[key: string]: HTMLTableRowElement}>({});
  const [draggedIndex, setDraggedIndex] = React.useState<number | null>(null);

  const renderSortIcon = (key: string) => {
    if (sortConfig.key === key) {
      return sortConfig.direction === 'asc' ? ' ▲' : ' ▼';
    }
    return null;
  };

  const onDragStart = (e: React.DragEvent<HTMLSpanElement>, index: number) => {
    setDraggedIndex(index);
    e.dataTransfer.setData('text/plain', index.toString());
  };

  const onDragOver = (e: React.DragEvent<HTMLTableRowElement>) => {
    e.preventDefault();
  };

  const onDrop = (e: React.DragEvent<HTMLTableRowElement>, targetIndex: number) => {
    e.preventDefault();
    if (draggedIndex !== null && draggedIndex !== targetIndex) {
      const newSources = [...sources];
      const [removed] = newSources.splice(draggedIndex, 1);
      newSources.splice(targetIndex, 0, removed);
      // Update the sources order in the parent component or API
      // actions.updateSourcesOrder(newSources);
    }
    setDraggedIndex(null);
  };

  const getSourceRoutes = (sourceName: string) => {
    const activeRoutes = routes.filter(route => route.source === sourceName && route.enabled);
    const disabledRoutes = routes.filter(route => route.source === sourceName && !route.enabled);
    return { activeRoutes, disabledRoutes };
  };

  const sortedSources = [...sources].sort((a, b) => {
    if (sortConfig.key === 'favorite') {
      const aStarred = starredSources.includes(a.name);
      const bStarred = starredSources.includes(b.name);
      return sortConfig.direction === 'asc' ? (aStarred === bStarred ? 0 : aStarred ? -1 : 1) : (aStarred === bStarred ? 0 : aStarred ? 1 : -1);
    }
    if (sortConfig.key === 'active') {
      const aActive = activeSource === a.name;
      const bActive = activeSource === b.name;
      return sortConfig.direction === 'asc' ? (aActive === bActive ? 0 : aActive ? -1 : 1) : (aActive === bActive ? 0 : aActive ? 1 : -1);
    }
    const aValue = a[sortConfig.key as keyof Source];
    const bValue = b[sortConfig.key as keyof Source];
    if (aValue === undefined || bValue === undefined) {
      return 0;
    }
    if (typeof aValue === 'string' && typeof bValue === 'string') {
      return sortConfig.direction === 'asc' ? aValue.localeCompare(bValue) : bValue.localeCompare(aValue);
    }
    if (aValue < bValue) return sortConfig.direction === 'asc' ? -1 : 1;
    if (aValue > bValue) return sortConfig.direction === 'asc' ? 1 : -1;
    return 0;
  });

  return (
    <div className="sources-list">
      <table className="sources-table">
        <thead>
          <tr>
            <th>Reorder</th>
            <th onClick={() => onSort('favorite')}>Favorite{renderSortIcon('favorite')}</th>
            <th onClick={() => onSort('active')}>Primary{renderSortIcon('active')}</th>
            <th onClick={() => onSort('name')}>Name{renderSortIcon('name')}</th>
            <th onClick={() => onSort('ip')}>IP Address{renderSortIcon('ip')}</th>
            <th onClick={() => onSort('enabled')}>Status{renderSortIcon('enabled')}</th>
            <th onClick={() => onSort('volume')}>Volume{renderSortIcon('volume')}</th>
            <th onClick={() => onSort('timeshift')}>Timeshift{renderSortIcon('timeshift')}</th>
            <th>Actions</th>
          </tr>
        </thead>
        <tbody>
          {sortedSources.map((source, index) => {
            const { activeRoutes, disabledRoutes } = getSourceRoutes(source.name);
            return (
              <SourceItem
                key={source.name}
                source={source}
                index={index}
                isStarred={starredSources.includes(source.name)}
                isActive={activeSource === source.name}
                actions={actions}
                sourceRefs={sourceRefs}
                onDragStart={onDragStart}
                onDragEnter={() => {}}
                onDragLeave={() => {}}
                onDragOver={onDragOver}
                onDrop={onDrop}
                onDragEnd={() => setDraggedIndex(null)}
                activeRoutes={activeRoutes}
                disabledRoutes={disabledRoutes}
              />
            );
          })}
        </tbody>
      </table>
    </div>
  );
};

export default SourceList;
