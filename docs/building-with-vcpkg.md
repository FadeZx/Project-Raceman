# Building Project Raceman with vcpkg

Project Raceman depends on GLFW and Assimp, while the GLAD loader is now
vendored in the repository. The Visual Studio project uses vcpkg manifest mode
to download and integrate the external libraries automatically. The steps below
assume that you already have the vcpkg tool
installed on your machine. If you do not have it yet, follow the official
instructions at <https://learn.microsoft.com/vcpkg> to install vcpkg and enable
Visual Studio integration.

## Steps

1. Clone this repository as usual.
2. Ensure that the environment variable `VCPKG_ROOT` points at your vcpkg
   installation and that you have enabled the "vcpkg manifest mode" integration
   in Visual Studio (this is the default when you run
   `vcpkg integrate install`).
3. Open `ProjectRaceman/Project Raceman.sln` in Visual Studio.
4. When you trigger a build for the first time Visual Studio will read the
   `vcpkg.json` manifest at the repository root, download the required
   dependencies, and configure the library/include search paths
   automatically.
5. Build the solution as usual (Debug or Release). The manifest makes sure that
   the linker finds the GLFW and Assimp symbols that previously caused
   unresolved external errors.

Once the packages have been installed by vcpkg you can build and run the
application without any additional manual configuration.
