import React, { useState, useEffect } from 'react';
import { Link, Outlet } from 'react-router-dom';

const Layout: React.FC = () => {
  const [notification, setNotification] = useState<{ message: string; type: 'success' | 'error' } | null>(null);

  useEffect(() => {
    if (notification) {
      const timer = setTimeout(() => {
        setNotification(null);
      }, 5000);

      return () => clearTimeout(timer);
    }
  }, [notification]);

  const showNotification = (message: string, type: 'success' | 'error') => {
    setNotification({ message, type });
  };

  return (
    <div className="layout">
      <header>
        <h1>ScreamRouter</h1>
        <nav>
          <ul>
            <li><Link to="/">Dashboard</Link></li>
            <li><Link to="/sources">Sources</Link></li>
            <li><Link to="/sinks">Sinks</Link></li>
            <li><Link to="/routes">Routes</Link></li>
            <li><a href="https://github.com/netham45/screamrouter/tree/master/Readme" target="_blank" rel="noopener noreferrer">Docs</a></li>
            <li><a href="https://github.com/netham45/screamrouter" target="_blank" rel="noopener noreferrer">GitHub</a></li>
          </ul>
        </nav>
      </header>
      <main>
        {notification && (
          <div className={`notification ${notification.type}`}>
            {notification.message}
          </div>
        )}
        <Outlet context={{ showNotification }} />
      </main>
      <footer>
        <p>&copy; 2024 Netham45</p>
      </footer>
    </div>
  );
};

export default Layout;