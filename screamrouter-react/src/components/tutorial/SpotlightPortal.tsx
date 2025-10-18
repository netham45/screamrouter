import React from 'react';
import { createPortal } from 'react-dom';

interface SpotlightPortalProps {
  rect: DOMRect | null;
  padding?: number;
  visible: boolean;
}

const SpotlightPortal: React.FC<SpotlightPortalProps> = ({ rect, padding = 12, visible }) => {
  if (typeof window === 'undefined' || !visible || !rect) {
    return null;
  }

  const expandedTop = Math.max(0, rect.top - padding);
  const expandedLeft = Math.max(0, rect.left - padding);
  const expandedRight = Math.min(window.innerWidth, rect.right + padding);
  const expandedBottom = Math.min(window.innerHeight, rect.bottom + padding);
  const expandedWidth = expandedRight - expandedLeft;
  const expandedHeight = expandedBottom - expandedTop;
  const cornerRadius = Math.min(16, Math.max(8, Math.min(expandedWidth, expandedHeight) * 0.1));

  const style: React.CSSProperties = {
    position: 'fixed',
    top: expandedTop,
    left: expandedLeft,
    width: expandedWidth,
    height: expandedHeight,
    borderRadius: cornerRadius,
    outline: '2px solid rgba(255, 255, 255, 0.85)',
    boxShadow: '0 0 30px 12px rgba(80, 170, 255, 0.25)',
    pointerEvents: 'none',
    transition: 'top 0.2s ease, left 0.2s ease, width 0.2s ease, height 0.2s ease',
    zIndex: 1001,
  };

  return createPortal(<div style={style} />, document.body);
};

export default SpotlightPortal;
