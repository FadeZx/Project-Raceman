# Project Raceman

Project Raceman is a Windows racing/game editor and runtime written in C++.
It provides an editor for creating Raceman projects, editing scenes, importing
assets, configuring physics/input, and packaging projects into standalone game
builds.

> Status: v0.1 pre-release. This build is intended for early testing and
> iteration.

## Features

- Editor launcher with recent projects, open project, and new project flow
- Default project template for creating clean Raceman projects
- Scene editor with hierarchy, inspector, scene/game viewports, and gizmo tools
- Mesh/material/shader asset workflows
- Vehicle, physics layer, and input profile configuration
- Jolt Physics integration
- Audio support through irrKlang
- Standalone game packaging from an editor project
- PowerShell build scripts for engine and game builds

## Download And Run

Download the latest release ZIP from the GitHub Releases page.

Then:

1. Extract the ZIP.
2. Extract `ProjectRacemanEngine.zip`.
3. Run `ProjectRaceman.exe` from the extracted engine folder.
4. Use `New Project` to create a fresh project, or extract/open
   `ExampleProject.zip` to explore the sample project.

If Windows SmartScreen warns about the executable, choose `More info` and
`Run anyway` only if you downloaded it from this repository's release page.

## Create Or Open A Project

Create a fresh project:

1. Launch `ProjectRaceman.exe`.
2. Click `New Project`.
3. Choose a parent folder.
4. Enter a project name.
5. The editor copies `templates/default-project` and opens the new project.

Open the included example project:

1. Extract `ExampleProject.zip` from the release download.
2. In Project Raceman, click `Open Project`.
3. Select the extracted `Example Project` folder.
4. The editor opens the sample project so you can inspect its scenes, assets,
   vehicle setup, and input settings.


## Build A Standalone Game

From the editor:

1. Open a Raceman project.
2. Choose `File > Build...`.
3. Select an output folder.
4. The editor packages the project into a runnable standalone folder.


## Build From Source

Requirements:

- Windows 10 or newer
- Visual Studio 2022 with the Desktop development with C++ workload
- MSBuild, installed with Visual Studio
- vcpkg with manifest mode enabled

The repository uses `vcpkg.json` for dependencies such as GLFW, GLAD, Assimp,
and Jolt Physics.

Build in Visual Studio:

1. Open `ProjectRaceman/Project Raceman.sln`.
2. Select `Debug|x64` or `Release|x64`.
3. Build the solution.

