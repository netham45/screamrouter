import React from 'react';
import { Link } from 'react-router-dom';

export const renderLinkWithAnchor = (to: string, name: string, icon: string) => (
  <Link to={`${to}#${to.replace(/^\//,'').replace(/s$/,'')}-${encodeURIComponent(name)}`}>
    <i className={`fas ${icon}`}></i> {name}
  </Link>
);

export const ActionButton: React.FC<{
  onClick: () => void;
  className?: string;
  children: React.ReactNode;
}> = ({ onClick, className, children }) => (
  <button onClick={onClick} className={className}>
    {children}
  </button>
);

export const VolumeSlider: React.FC<{
  value: number;
  onChange: (value: number) => void;
}> = ({ value, onChange }) => (
  <input
    type="range"
    min="0"
    max="1"
    step="0.01"
    value={value}
    onChange={(e) => onChange(parseFloat(e.target.value))}
  />
);