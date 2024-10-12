import React, { forwardRef } from 'react';

interface CollapsibleSectionProps {
  title: string | JSX.Element;
  subtitle?: string;
  isExpanded: boolean;
  onToggle: () => void;
  children: React.ReactNode;
  id?: string;
}

export const CollapsibleSection = forwardRef<HTMLDivElement, CollapsibleSectionProps>(({ 
  title, 
  subtitle, 
  isExpanded, 
  onToggle, 
  children, 
  id 
}, ref) => {
  return (
    <div className={`collapsible-section ${isExpanded ? 'expanded' : 'collapsed'}`} ref={ref}>
      <div className="section-header" onClick={onToggle} id={id}>
        <h3>{title}{subtitle && <span className="section-subtitle"> {subtitle}</span>}</h3>
        <div className="expand-toggle">▶</div>
      </div>
      <div className="section-content">{children}</div>
    </div>
  );
});

CollapsibleSection.displayName = 'CollapsibleSection';
