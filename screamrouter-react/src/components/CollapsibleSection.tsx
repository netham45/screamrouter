/**
 * React component for a collapsible section.
 * This component provides a UI element that can be expanded or collapsed to show or hide its content.
 * It includes a header with a title and an optional subtitle, which toggles the visibility of the content when clicked.
 *
 * @param {CollapsibleSectionProps} props - The properties for the component.
 * @param {string | JSX.Element} props.title - The main title of the section.
 * @param {string | undefined} props.subtitle - Optional. A subtitle to display next to the title.
 * @param {boolean} props.isExpanded - Indicates whether the section is currently expanded or collapsed.
 * @param {() => void} props.onToggle - Callback function to toggle the expansion state of the section.
 * @param {React.ReactNode} props.children - The content to be displayed inside the collapsible section.
 * @param {string | undefined} props.id - Optional. An ID for the section header element.
 */
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
