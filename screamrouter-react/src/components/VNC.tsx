import React, { useEffect, useRef } from 'react';
import ApiService, { Source } from '../api/api';

interface VNCProps {
  source: Source;
}

const VNC: React.FC<VNCProps> = ({ source }) => {
  const outerIframeRef = useRef<HTMLIFrameElement>(null);

  useEffect(() => {
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
        outerIframe.style.width = `${width}px`;
        outerIframe.style.height = `${height}px`;
        innerIframe.style.width = `${width}px`;
        innerIframe.style.height = `${height}px`;
      } else {
        outerIframe.style.width = "400px";
        outerIframe.style.height = "600px";
      }
    };

    const resizeInterval = setInterval(resizeIframes, 1000);
    resizeIframes();
    [200, 400, 600, 800].forEach(delay => setTimeout(resizeIframes, delay));

    return () => clearInterval(resizeInterval);
  }, []);

  const vncUrl = ApiService.getVncUrl(source.name);

  return (
    <div className="vnc-container">
      <h3>VNC for {source.name}</h3>
      <iframe
        ref={outerIframeRef}
        src={vncUrl}
        title={`VNC for ${source.name}`}
        className="vnc-iframe"
      />
    </div>
  );
};

export default VNC;