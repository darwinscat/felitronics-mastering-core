### session — adopting the machine stamps this core

- **`AdoptMachine` stamps the project with this release.** After an import from another core, adopting the planner's
  machine layer left `project.core` at the file's release, though this release placed the layer: the master's recipe
  and the exported file named the old core. The command now stamps `core` before it places, as every other placement
  does, so the adopted project's master is the fresh session's master, recipe included. Held by
  `ScenarioTests.cpp:theFilesMachine`.
