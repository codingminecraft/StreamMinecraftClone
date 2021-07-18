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

    targetdir("bin/" .. outputdir .. "/%{prj.name}")
    objdir("bin-int/" .. outputdir .. "/%{prj.name}")

    files {
        "Minecraft/src/**.cpp",
        "Minecraft/include/**.h",
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
		"Minecraft/vendor/yamlCpp/include/**.h"
    }

    includedirs {
        "Minecraft/include",
        "Minecraft/vendor/GLFW/include",
        "Minecraft/vendor/glad/include",
        "Minecraft/vendor/CppUtils/SingleInclude/",
        "Minecraft/vendor/glm/",
        "Minecraft/vendor/stb/",
        "Minecraft/vendor/yamlCpp/include"
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

