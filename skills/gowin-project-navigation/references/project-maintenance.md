# Maintaining a Gowin project

Read this only for Git-aware lookup, project moves or renames, repository organization, or file-lock recovery. For Windows file-lock recovery, read [the Windows guide](project-move-windows.md).

## Establish the project package

- Resolve the Git root and selected `.gprj` separately. Git status and history can explain moved files, but they do not determine active Gowin project membership.
- Treat the `.gprj` directory and its referenced source, IP, constraint, and configuration paths as one project package. Identify shared or external paths before moving it.
- Classify files by manifest relationship, location, producer, and role rather than extension or Git tracking state. If a role is unclear, read the [artifact map](project-artifact-map.md).

## Change paths safely

- Prefer moving or renaming the outer project directory intact. When internal paths must change, update the owning `.gprj` and related IP, configuration, or script references deliberately.
- Preserve generated outputs on disk unless deletion is requested. Changing tracking or ignore rules is a separate repository operation, not a consequence of classifying an artifact as generated.
- After an authorized move, reread affected project entries and inspect Git status for missing, added, or partially moved paths.
