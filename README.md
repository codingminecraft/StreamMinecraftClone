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

### Current Bugs
    Current BUGS 26 October CST:
    BUG #1:
        Description of the bug: You glitch into the world in physics mode.
        Steps to reproduce the behavior:
            1. Mine 2 blocks down.
            2. Place a block at the bottom without jumping.
            3. You glitch into the world.
        Expected behaviour:
            1. You are supposed to not place a block there for that to happen or #2
            2. You are supposed to automatically jump.
        OS:
            ***ENGINE CURRENTLY SUPPORTS ONLY WINDOWS***
    BUG #2:
        Description of the bug: You glitch into the world in physics mode.
        Steps to reproduce the behavior:
            1. Mine 2 blocks down.
            2. Place a block at the head level of the player.
            3. You glitch into the world./
        Expected behaviour:
            1. You are supposed to not place a block there for that to happen or #2
        OS:
            ***ENGINE CURRENTLY SUPPORTS ONLY WINDOWS***
    BUG #3:
        Description of the bug: Textures disappear.
        Steps to reproduce the behavior:
            1. Select item slot 9 (or) 8
            2. Place it
            3. It only disappears rarely but it is still a bug.
        Expected behaviour:
            1. You are supposed to place the block fully with the textures.
        OS:
            ***ENGINE CURRENTLY SUPPORTS ONLY WINDOWS***
    BUG #3:
        Description of the bug: Font not correctly rendering. like for example, "e" is in the middle of the letter height instead of being at the bottom
        Steps to reproduce: ***NO STEPS TO REPRODUCE IT OCCURS AT THE START OR THE GAME***.
        Expected behaviour:
            1. It are supposed to render the letters at their correct `Y` position. 
        OS:
            ***ENGINE CURRENTLY SUPPORTS ONLY WINDOWS***
    BUG #4:
        Description of the bug: You glitch into the world in spectator mode.
        Steps to reproduce the behavior:
            1. Go to spectator mode by pressing `F4`.
            2. Go down into a block.
            3. Go to physics mode. by pressing `F4`.
            4. You glitch into the world. Same as bug #1
        Expected behaviour:
            1. You are not supposed to be able to switch to physics mode while in a block.
        OS:
            ***ENGINE CURRENTLY SUPPORTS ONLY WINDOWS***
    BUG #5:
        Description of the bug: The game breaks in certain computers, the font does not show up.
        Steps to reproduce the behavior: ***NO STEPS TO REPRODUCE THE BUG AVAILABLE***
        Expected behaviour:
            2. You are supposed to render the font.
        OS:
            ***ENGINE CURRENTLY SUPPORTS ONLY WINDOWS***
        Additional context: `LOG: Missing glyph of font '127'`
