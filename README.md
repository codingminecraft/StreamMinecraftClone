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
