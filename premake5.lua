workspace "Minecraft"
    architecture "x64"

    configurations {
        "Debug",
        "Release",
        "Dist"
    }

    startproject "Minecraft"

-- This is a helper variable, to concatenate the sys-arch
outputdir = "%{cfg.buildcfg}-%{cfg.system}-%{cfg.architecture}"

project "Minecraft"
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++17"
    staticruntime "on"

    targetdir("bin\\" .. outputdir .. "\\%{prj.name}")
    objdir("bin-int\\" .. outputdir .. "\\%{prj.name}")

    files {
        "Minecraft/src/**.cpp",
        "Minecraft/include/**.h",
        "Minecraft/include/**.hpp",
        "Minecraft/src/**.hpp",
        "Minecraft/vendor/GLFW/include/GLFW/glfw3.h",
        "Minecraft/vendor/GLFW/include/GLFW/glfw3native.h",
        "Minecraft/vendor/GLFW/src/glfw_config.h",
        "Minecraft/vendor/GLFW/src/context.c",
        "Minecraft/vendor/GLFW/src/init.c",
        "Minecraft/vendor/GLFW/src/input.c",
        "Minecraft/vendor/GLFW/src/monitor.c",
        "Minecraft/vendor/GLFW/src/vulkan.c",
        "Minecraft/vendor/GLFW/src/window.c",
        "Minecraft/vendor/glad/include/glad/glad.h",
        "Minecraft/vendor/glad/include/glad/KHR/khrplatform.h",
		"Minecraft/vendor/glad/src/glad.c",
        "Minecraft/vendor/cppUtils/SingleInclude/CppUtils/CppUtils.h",
        "Minecraft/vendor/glm/glm/**.hpp",
		"Minecraft/vendor/glm/glm/**.inl",
        "Minecraft/vendor/stb/stb_image.h",
        "Minecraft/vendor/yamlCpp/src/**.h",
		"Minecraft/vendor/yamlCpp/src/**.cpp",
		"Minecraft/vendor/yamlCpp/include/**.h",
        "Minecraft/vendor/simplex/src/**.h",
        "Minecraft/vendor/simplex/src/**.cpp",
        "Minecraft/vendor/optick/src/**.cpp",
        "Minecraft/vendor/optick/src/**.h",
    }

    removefiles {
        "Minecraft/vendor/optick/src/optick_gpu.cpp",
        "Minecraft/vendor/optick/src/optick_gpu.d3d12.cpp",
        "Minecraft/vendor/optick/src/optick_gpu.vulkan.cpp"
    }

    includedirs {
        "Minecraft/include",
        "Minecraft/vendor/GLFW/include",
        "Minecraft/vendor/glad/include",
        "Minecraft/vendor/glm/",
        "Minecraft/vendor/stb/",
        "Minecraft/vendor/yamlCpp/include",
        "Minecraft/vendor/simplex/src",
        "Minecraft/vendor/cppUtils/single_include",
        "Minecraft/vendor/freetype/include",
        "Minecraft/vendor/magicEnum/include",
        "Minecraft/vendor/optick/src",
        "Minecraft/vendor/robinHoodHashing/src/include"
    }

    libdirs {
        "\"./Minecraft/vendor/freetype/release dll/win64\""
    }

    links {
        "freetype.lib"
    }

    defines {
        "_SILENCE_ALL_CXX17_DEPRECATION_WARNINGS"
    }

    filter "system:windows"
        buildoptions { "-lgdi32" }
        systemversion "latest"

        files {
            "Minecraft/vendor/GLFW/src/win32_init.c",
            "Minecraft/vendor/GLFW/src/win32_joystick.c",
            "Minecraft/vendor/GLFW/src/win32_monitor.c",
            "Minecraft/vendor/GLFW/src/win32_time.c",
            "Minecraft/vendor/GLFW/src/win32_thread.c",
            "Minecraft/vendor/GLFW/src/win32_window.c",
            "Minecraft/vendor/GLFW/src/wgl_context.c",
            "Minecraft/vendor/GLFW/src/egl_context.c",
            "Minecraft/vendor/GLFW/src/osmesa_context.c"
        }

        defines  {
            "_GLFW_WIN32",
            "_CRT_SECURE_NO_WARNINGS",
            "_ITERATOR_DEBUG_LEVEL=0",
            "_SECURE_SCL=0",
            "_HAS_ITERATOR_DEBUGGING=0",
            "_NO_DEBUG_HEAP=1"
        }

        postbuildcommands {
            "copy /y \"Minecraft\\vendor\\freetype\\release dll\\win64\\freetype.dll\" \"%{cfg.targetdir}\\freetype.dll\"",
            "copy /y \"Minecraft\\vendor\\freetype\\release dll\\win64\\freetype.lib\" \"%{cfg.targetdir}\\freetype.lib\""
        }

    filter { "configurations:Debug" }
        buildoptions "/MTd"
        runtime "Debug"
        symbols "on"

    filter { "configurations:Release" }
        defines {" _RELEASE" }
        buildoptions "/MT"
        runtime "Release"
        optimize "on"



project "Bootstrap"
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++17"
    staticruntime "off"

    targetdir("bin/" .. outputdir .. "/%{prj.name}")
    objdir("bin-int/" .. outputdir .. "/%{prj.name}")

    files {
        "Bootstrap/src/**.cpp",
        "Bootstrap/include/**.h",
        "Bootstrap/vendor/bit7z/include/**.hpp"
    }

    includedirs {
        "Bootstrap/include",
        "Bootstrap/vendor/curl/include",
        "Minecraft/vendor/cppUtils/single_include/",
        "Bootstrap/vendor/bit7z/include/"
    }

    filter "system:windows"
        buildoptions { "-lgdi32" }
        systemversion "latest"

        libdirs {
            "./Bootstrap/vendor/curl/lib",
            "./Bootstrap/vendor/bit7z/lib"
        }

        links {
            "libcurl.dll.lib",
            "oleaut32",
            "user32"
        }

        filter { "configurations:Debug" }
            links {
                "bit7z64_d"
            }
        filter { "configurations:Release" }
            links {
                "bit7z64"
            }

        defines  {
            "_CRT_SECURE_NO_WARNINGS"
        }

    filter { "configurations:Debug" }
        buildoptions "/MTd"
        runtime "Debug"
        symbols "on"

    filter { "configurations:Release" }
        buildoptions "/MT"
        runtime "Release"
        optimize "on"