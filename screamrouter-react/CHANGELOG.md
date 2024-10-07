# Changelog

## [Unreleased]

### Added
- Implemented anchor tags for each source, route, and sink in their respective components.
- Added linking functionality between related sources, routes, and sinks.
- Implemented a gentle flashing effect for 3 seconds when an anchor is referenced.
- Added drag-and-drop functionality to reorder sources, sinks, and routes using react-beautiful-dnd.
- Implemented automatic scrolling and flashing when items are modified (enabled/disabled/starred).

### Changed
- Updated Sources.tsx, Sinks.tsx, and Routes.tsx components to include new anchor and drag-and-drop functionality.
- Modified the jumpToAnchor function in each component to include the flashing effect.
- Updated the onDragEnd functions in each component to use the reorder API calls.

### Updated
- Modified index.css to include styles for the flashing effect and anchor tags.
- Ensured that the api.ts file includes the necessary reorder API calls for sources, sinks, and routes.

## [Previous Version]

# Note: Replace [Previous Version] with the actual previous version number, and [Unreleased] with the new version number when releasing.