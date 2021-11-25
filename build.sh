#!/bin/bash
if [[ $# > 0 && $1 != 'help' && $1 != 'h' ]]; then 
    # Build the project files
    vendor/premake5 $1

    if [ ! -e Minecraft/vendor/freetype ]; then
        echo 'Downloading any libraries required.'
        ./vendor/Bootstrap/Bootstrap.out
    fi

else
    echo
    echo -e 'Enter "build.bat action" where action is one of the following:'
    echo
    echo -e '\tcodelite          Generate CodeLite project files'
    echo -e '\tgmake             Generate GNU makefiles for Linux'
    echo -e '\tvs2005            Generate Visual Studio 2005 project files'
    echo -e '\tvs2008            Generate Visual Studio 2008 project files'
    echo -e '\tvs2010            Generate Visual Studio 2010 project files'
    echo -e '\tvs2012            Generate Visual Studio 2012 project files'
    echo -e '\tvs2013            Generate Visual Studio 2013 project files'
    echo -e '\tvs2015            Generate Visual Studio 2015 project files'
    echo -e '\tvs2017            Generate Visual Studio 2017 project files'
    echo -e '\tvs2019            Generate Visual Studio 2019 project files'
    echo -e '\txcode4            Generate Apple Xcode 4 project files'
fi