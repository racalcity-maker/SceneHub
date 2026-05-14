# SceneHub Desktop App

This folder is the dedicated workspace for the SceneHub desktop application.

It should contain:

- desktop product and UX plans;
- desktop technical architecture notes;
- frontend application source code;
- desktop-specific assets and tooling.

Current structure:

- `docs/` - desktop application planning and architecture documents.
- `src/` - React/TypeScript desktop frontend source code.
- `src-tauri/` - Tauri shell project files.

Bootstrap commands:

- `npm install`
- `npm run dev`
- `npm run tauri:dev`

This folder is intentionally separate from firmware-focused `docs/` so desktop
work can evolve as its own project stream without mixing product/UI planning
into the embedded documentation index.
