import React, { useRef } from 'react';
import ContentHeader from './ContentHeader';
import DashboardContent from '../content/DashboardContent';
import ActiveSourceContent from '../content/ActiveSourceContent';
import NowListeningContent from '../content/NowListeningContent';
import SourcesContent from '../content/SourcesContent';
import SinksContent from '../content/SinksContent';
import RoutesContent from '../content/RoutesContent';
import FavoritesContent from '../content/FavoritesContent';
import SourceContent from '../content/SourceContent';
import SinkContent from '../content/SinkContent';
import RouteContent from '../content/RouteContent';
import StatsPage from '../../pages/StatsPage';
import { ContentProps, ContentCategory, ViewMode } from '../types';
import { Actions } from '../../../utils/actions';

/**
 * ContentPanel component for the FullMenu.
 * This component displays the content based on the current category.
 */
interface ContentPanelProps extends Omit<ContentProps, 'viewMode' | 'sortConfig' | 'actions'> {
  currentCategory: ContentCategory;
  viewMode: ViewMode;
  setViewMode: (mode: ViewMode) => void;
  onSort: (key: string) => void;
  sortConfig: { key: string; direction: 'asc' | 'desc' };
  isDarkMode?: boolean;
  actions: Actions;
}

const ContentPanel: React.FC<ContentPanelProps> = ({
  currentCategory,
  viewMode,
  setViewMode,
  onSort,
  sortConfig,
  isDarkMode,
  actions,
  ...contentProps
}) => {
  const contentRef = useRef<HTMLDivElement>(null);

  /**
   * Function to render the content based on the current category.
   */
  const renderContent = () => {
    // Create a complete props object with viewMode, sortConfig, and actions
    const completeProps: ContentProps = {
      ...contentProps,
      viewMode,
      sortConfig,
      isDarkMode,
      actions: actions
    };

    switch (currentCategory) {
      case 'dashboard':
        return <DashboardContent {...completeProps} />;
      case 'active-source':
        return <ActiveSourceContent {...completeProps} />;
      case 'now-listening':
        return <NowListeningContent {...completeProps} />;
      case 'sources':
        return <SourcesContent {...completeProps} />;
      case 'sinks':
        return <SinksContent {...completeProps} />;
      case 'routes':
        return <RoutesContent {...completeProps} />;
      case 'favorites':
        return <FavoritesContent {...completeProps} />;
      case 'source':
        return <SourceContent {...completeProps} />;
      case 'sink':
        return <SinkContent {...completeProps} />;
      case 'route':
        return <RouteContent {...completeProps} />;
     case 'stats':
       return <StatsPage />;
      default:
        return <DashboardContent {...completeProps} />;
    }
  };

  return (
    <div className="content-panel">
      <ContentHeader
        currentCategory={currentCategory}
        viewMode={viewMode}
        setViewMode={setViewMode}
        sortConfig={sortConfig}
        onSort={onSort}
        isDarkMode={isDarkMode}
      />
      
      <div className="content-body" ref={contentRef}>
        {renderContent()}
      </div>
    </div>
  );
};

export default ContentPanel;