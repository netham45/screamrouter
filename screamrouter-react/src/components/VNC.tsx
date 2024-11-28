import React, { useEffect, useRef, useState } from 'react';
import ApiService, { Source } from '../api/api';
import "../styles/Layout.css"

/**
 * Props for the VNC component
 * @interface VNCProps
 * @property {Source} source - The source for which to display the VNC connection
 */
interface VNCProps {
  source: Source;
}

/**
 * VNC component for displaying a Virtual Network Computing connection
 * @param {VNCProps} props - The component props
 * @returns {React.FC} A functional component for displaying VNC
 */
const VNC: React.FC<VNCProps> = ({ source }) => {
  const outerIframeRef = useRef<HTMLIFrameElement>(null);
  const [error, setError] = useState<string | null>(null);

  /**
   * Handles errors that may occur when loading the VNC iframe
   * @param {React.SyntheticEvent<HTMLIFrameElement, Event>} event - The error event
   */
  const handleIframeError = (event: React.SyntheticEvent<HTMLIFrameElement, Event>) => {
    console.error('Error loading VNC iframe:', event);
    setError('Failed to load VNC connection. Please try again.');
  };

  const vncUrl = ApiService.getVncUrl(source.name);
  useEffect(() => {
    document.title = `ScreamRouter - ${source.name}`;
  }, [source.name]);

  useEffect(() => {
    document.body.style.overflow = 'hidden';
  }, [source.name]);

  return (
    <div className="vnc-container">
      {error && <div className="error-message">{error}</div>}
      <iframe
        ref={outerIframeRef}
        src={vncUrl}
        title={`${source.name}`}
        className="vnc-iframe"
        onError={handleIframeError}
      />
    </div>
  );
};

export default VNC;