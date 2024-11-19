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

  useEffect(() => {
    /**
     * Resizes the iframes to fit the VNC canvas
     */
    const resizeIframes = () => {
      const outerIframe = outerIframeRef.current;
      if (!outerIframe || !outerIframe.contentWindow) return;

      const innerIframe = outerIframe.contentWindow.document.querySelector('iframe');
      if (!innerIframe || !innerIframe.contentWindow) return;

      const canvas = innerIframe.contentWindow.document.querySelector('canvas');
      if (!canvas) return;

      const maxWidth = window.innerWidth;
      const maxHeight = window.innerHeight;
      const width = Math.min(canvas.width, maxWidth);
      const height = Math.min(canvas.height, maxHeight);

      if (canvas.width > 0 && canvas.height > 0) {
        outerIframe.style.width = `${width * 1.05}px`;
        outerIframe.style.height = `${height * 1.05}px`;
        innerIframe.style.width = `${width * 1.05}px`;
        innerIframe.style.height = `${height * 1.05}px`;
        innerIframe.style.border = "0px";
      } else {
        outerIframe.style.width = "400px";
        outerIframe.style.height = "600px";
        innerIframe.style.border = "0px";
      }
    };

    // Set up interval for continuous resizing
    //const resizeInterval = setInterval(resizeIframes, 1000);
    
    // Initial resize and additional resize attempts
    //resizeIframes();
    //[200, 400, 600, 800].forEach(delay => setTimeout(resizeIframes, delay));

    // Clean up interval on component unmount
    //return () => clearInterval(resizeInterval);
  }, []);

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