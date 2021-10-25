# Minecraft Clone

This is a Minecraft clone that will be used for an education YouTube series. I will link the YouTube series here once I begin creating it.

## To Build

> Note: Currently this code only supports Windows

In order to build this, you must have git installed. Then you can create a new directory where you want to install this code and open a command prompt. Then run:

```batch
git clone --recursive https://github.com/codingminecraft/StreamMinecraftClone
cd StreamMinecraftClone
build.bat vs2019
```

This should create a Visual Studio solution file, and all you need to do is double click the solution file, then build and run the project within Visual Studio.

> Note: If you want to use a different build system, just run build.bat which will print out all available build systems.

## Bug Reporting

This game is still very much in development, there are no official releases yet. However, bug reporting is still very helpful so I can keep track of everything. If you encounter any bugs please report them at the issues tab of the repository at: https://github.com/codingminecraft/StreamMinecraftClone.

Follow this template when reporting the bug:

```markdown 
## Describe the bug
A clear and concise description of what the bug is.

## To Reproduce
Steps to reproduce the behavior, for example:

1. Go to '...'
2. Click on '....'
3. Scroll down to '....'
4. See error

## Expected behavior
A clear and concise description of what you expected to happen.

## Screenshots
If applicable, add screenshots to help explain your problem.

## Operating System (please complete the following information):
OS: [e.g. Windows 10, Linux, MacOS]

## Additional context
Add any other context about the problem here.
```

## TODOs

This is a list of tasks that still need to be done.

### Most Important
---

- [x] ~~Save chunks when they unload from the chunk radius~~
- [ ] Make GUI stuff look pretty
- [ ] Clean up the code in `ChunkManager.cpp`
- [ ] Make the application handle out of memory errors gracefully (Sort of done...)
    * Either make it allocate more memory, or just de-prioritize the furthest chunks
- [ ] Add block level lighting
    * Add light sources
    * Add multi-color lighting
- [ ] Add proper transparency support
- [ ] Add inventory management
    * Add icons for blocks in the inventory
    * Add indicators for number of blocks in a slot
- [ ] Add crafting support
- [ ] Cubemaps
- [ ] Day/Night cycle
- [ ] Biome generation
    * Ore generation
    * Structure generation
    * Tree generation
- [ ] Water/Lava
- [x] ~~Fullscreen support~~
- [ ] Add sounds

### Less Important
---

- [ ] Support for different block types
    * E.g stairs, slabs, door, beds, etc.
- [ ] 3D Rigged models and animations
- [ ] Mobs
- [ ] True survival mode
- [ ] Settings Menu
