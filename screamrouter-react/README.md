# ScreamRouter React Frontend

This project is a React-based frontend for the ScreamRouter application, which routes PCM audio for Scream sinks and sources.

## Features

- Dashboard with visual representation of active sources, sinks, and routes
- Management of Sources, Sinks, and Routes
  - Enable/disable sources, sinks, and routes
  - Adjust volume for sources, sinks, and routes
  - Add, edit, and delete sources, sinks, and routes
- Equalizer functionality for Sources, Sinks, and Routes
- VNC integration for compatible sources
- Milkdrop visualizations for audio output
- Ability to star favorite Sources, Sinks, and Routes
- HTTP streaming for listening to sinks
- Group management for sources and sinks
- Responsive design for various screen sizes

## Prerequisites

- Node.js (v14 or later)
- npm (v6 or later)

## Installation

1. Clone the repository:
   ```
   git clone https://github.com/your-repo/screamrouter-react.git
   cd screamrouter-react
   ```

2. Install dependencies:
   ```
   npm install
   ```

3. Create a `.env` file in the root directory and add the following content:
   ```
   REACT_APP_API_URL=http://your-api-url:port/
   ```
   Replace `http://your-api-url:port/` with the actual URL of your ScreamRouter API.

## Running the Application

To start the development server:

```
npm start
```

The application will be available at `http://localhost:3000/site`.

## Building for Production

To create a production build:

```
npm run build
```

This will create a `build` folder with production-ready files.

## Project Structure

- `src/components/`: React components (Dashboard, Sources, Sinks, Routes, VNC, Equalizer, MilkdropVisualizer)
- `src/styles/`: CSS files for styling components
- `src/api/`: API service for backend communication
- `src/types/`: TypeScript type definitions

## Key Components

- `Dashboard`: Displays an overview of active sources, sinks, and routes with visual connections
- `Sources`: Manages audio sources, including enabling/disabling, volume control, and VNC access
- `Sinks`: Manages audio sinks, including enabling/disabling, volume control, and HTTP streaming
- `Routes`: Manages audio routes between sources and sinks
- `VNC`: Provides VNC functionality for compatible sources
- `Equalizer`: Allows adjustment of equalizer settings for sources, sinks, and routes
- `MilkdropVisualizer`: Provides visual representations of audio output

## Contributing

Please read [CONTRIBUTING.md](CONTRIBUTING.md) for details on our code of conduct, and the process for submitting pull requests.

## License

This project is licensed under the MIT License - see the [LICENSE.md](LICENSE.md) file for details.
