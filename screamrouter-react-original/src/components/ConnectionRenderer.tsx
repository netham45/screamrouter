import React, { useRef, useEffect, useState } from 'react';
import { Route } from '../api/api';
import { useNavigate } from 'react-router-dom';

interface ConnectionRendererProps {
  routes: Route[];
}

const ConnectionRenderer: React.FC<ConnectionRendererProps> = ({ routes }) => {
  const svgRef = useRef<SVGSVGElement>(null);
  const navigate = useNavigate();
  const [dimensions, setDimensions] = useState({ width: 0, height: 0 });

  useEffect(() => {
    const updateDimensions = () => {
      if (svgRef.current) {
        const { width, height } = svgRef.current.getBoundingClientRect();
        setDimensions({ width, height });
      }
    };

    updateDimensions();
    window.addEventListener('resize', updateDimensions);
    return () => window.removeEventListener('resize', updateDimensions);
  }, []);

  useEffect(() => {
    renderConnections();
  }, [routes, dimensions]);

  const renderConnections = () => {
    if (!svgRef.current) return;

    const svg = svgRef.current;
    svg.innerHTML = '';

    routes.forEach((route) => {
      const sourceEl = document.getElementById(`source-${route.source}`);
      const sinkEl = document.getElementById(`sink-${route.sink}`);
      if (sourceEl && sinkEl) {
        const sourceRect = sourceEl.getBoundingClientRect();
        const sinkRect = sinkEl.getBoundingClientRect();
        const svgRect = svg.getBoundingClientRect();

        const x1 = sourceRect.right - svgRect.left;
        const y1 = sourceRect.top + sourceRect.height / 2 - svgRect.top;
        const x2 = sinkRect.left - svgRect.left;
        const y2 = sinkRect.top + sinkRect.height / 2 - svgRect.top;

        const path = document.createElementNS('http://www.w3.org/2000/svg', 'path');
        const midX = (x1 + x2) / 2;
        const controlY1 = y1 - 50;
        const controlY2 = y2 - 50;
        const d = `M ${x1} ${y1} C ${midX} ${controlY1}, ${midX} ${controlY2}, ${x2} ${y2}`;
        
        path.setAttribute('d', d);
        path.setAttribute('fill', 'none');
        path.setAttribute('stroke', '#2ecc71');
        path.setAttribute('stroke-width', '2');
        path.setAttribute('style', 'pointer-events: all; cursor: pointer;');
        path.classList.add('route-line', 'clickable');
        path.dataset.routeName = route.name;
        path.setAttribute('title', `Route: ${route.name}`);

        path.addEventListener('click', (e) => {
          e.preventDefault();
          navigate(`/routes#route-${encodeURIComponent(route.name)}`);
        });

        path.addEventListener('mouseenter', () => {
          path.setAttribute('stroke', '#3498db');
          path.setAttribute('stroke-width', '3');
        });

        path.addEventListener('mouseleave', () => {
          path.setAttribute('stroke', '#2ecc71');
          path.setAttribute('stroke-width', '2');
        });

        svg.appendChild(path);
      }
    });
  };

  return (
    <svg 
      ref={svgRef} 
      className="connections" 
      style={{ 
        pointerEvents: 'none', 
        zIndex: 1,
        width: '100%',
        height: '100%',
        position: 'absolute',
        top: 0,
        left: 0
      }}
    ></svg>
  );
};

export default ConnectionRenderer;