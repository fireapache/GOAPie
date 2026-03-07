--[[

	Premake5 script for GOAPie and its dependicies.

	Projects:
		GOAPie
		GOAPie Tests

	Dependencies:
		GLAD
		GLFW
		IMGUI

	Windows:
		Debug, Release
		Win32, x64

	Linux: TODO!

--]]

workspace "GOAPie"
	
	configurations { "Debug", "Release" }
	platforms { "Win32", "x64" }

	startproject "Tests"
	defaultplatform "x64"

	multiprocessorcompile "On"

	filter { "platforms:Win32" }
		system "Windows"
		architecture "x86"

	filter { "platforms:x64" }
		system "Windows"
		architecture "x86_64"

outputdir = "%{cfg.buildcfg}-%{cfg.system}-%{cfg.architecture}/"

newoption {
	trigger = "unity",
	description = "Enabled unity build configuration."
}

group "Dependencies"

project "GLAD"
	
	location "Source/GLAD/"
	kind "StaticLib"
	language "C"
	targetdir ("Intermediate/%{prj.name}-" .. outputdir)
	objdir ("Intermediate/%{prj.name}-" .. outputdir)
	
	defines {  }

	libdirs {  }

	includedirs { "%{prj.location}/include/" }

	files
	{
		"%{prj.location}/include/**.h",
		"%{prj.location}/src/glad.c"
	}

	filter "system:windows"
		cppdialect "C++17"
		staticruntime "On"
		systemversion "latest"
		defines {  }

		files
		{

		}

	filter "configurations:Debug"
		symbols "On"

	filter "configurations:Release"
		optimize "On"

project "GLFW"
	
	location "Source/GLFW/"
	kind "StaticLib"
	language "C"
	targetdir ("Intermediate/%{prj.name}-" .. outputdir)
	objdir ("Intermediate/%{prj.name}-" .. outputdir)
	
	defines {  }

	libdirs {  }

	files
	{
		"%{prj.location}/src/internal.h",
		"%{prj.location}/src/mappings.h",
        "%{prj.location}/src/glfw_config.h",
        "%{prj.location}/include/GLFW/glfw3.h",
        "%{prj.location}/include/GLFW/glfw3native.h",
		"%{prj.location}/src/context.c",
		"%{prj.location}/src/init.c",
        "%{prj.location}/src/input.c",
		"%{prj.location}/src/monitor.c",
		"%{prj.location}/src/vulkan.c",
        "%{prj.location}/src/window.c"
	}

	filter "system:windows"
		staticruntime "On"
		systemversion "latest"
		defines { "_GLFW_WIN32", "_CRT_SECURE_NO_WARNINGS" }

		files
		{
			"%{prj.location}/src/win32_platform.h",
			"%{prj.location}/src/win32_joystick.h",
			"%{prj.location}/src/win32_joystick.c",
			"%{prj.location}/src/win32_init.c",
			"%{prj.location}/src/win32_monitor.c",
			"%{prj.location}/src/win32_module.c",
			"%{prj.location}/src/null_init.c",
			"%{prj.location}/src/null_joystick.c",
			"%{prj.location}/src/null_joystick.h",
			"%{prj.location}/src/null_monitor.c",
			"%{prj.location}/src/null_platform.h",
			"%{prj.location}/src/null_window.c",
			"%{prj.location}/src/win32_time.c",
			"%{prj.location}/src/win32_thread.c",
			"%{prj.location}/src/win32_window.c",
			"%{prj.location}/src/wgl_context.h",
			"%{prj.location}/src/wgl_context.c",
			"%{prj.location}/src/egl_context.h",
			"%{prj.location}/src/egl_context.c",
			"%{prj.location}/src/osmesa_context.h",
			"%{prj.location}/src/osmesa_context.c",
			"%{prj.location}/src/platform.c"
		}

	filter "configurations:Debug"
		symbols "On"
		runtime "Debug"

	filter "configurations:Release"
		optimize "On"
		runtime "Release"

project "IMGUI"
	
	location "Source/IMGUI/"
	kind "StaticLib"
	language "C"
	targetdir ("Intermediate/%{prj.name}-" .. outputdir)
	objdir ("Intermediate/%{prj.name}-" .. outputdir)
	
	defines {  }

	libdirs
	{
		("Intermediate/GLAD-" .. outputdir),
		("Intermediate/GLFW-" .. outputdir)
	}

	dependson { "GLAD", "GLFW" }

	includedirs
	{
		"%{prj.location}/",
		"%{prj.location}/backends",
		"Source/GLAD/include/",
		"Source/GLFW/include/"
	}

	files
	{
		"%{prj.location}/imgui.h",
		"%{prj.location}/imgui.cpp",
		"%{prj.location}/imgui_draw.cpp",
		"%{prj.location}/imgui_internal.h",
		"%{prj.location}/imgui_widgets.cpp",
		"%{prj.location}/imgui_tables.cpp",
		"%{prj.location}/imgui_demo.cpp",
		"%{prj.location}/backends/imgui_impl_glfw.h",
		"%{prj.location}/backends/imgui_impl_glfw.cpp",
		"%{prj.location}/backends/imgui_impl_opengl3.h",
		"%{prj.location}/backends/imgui_impl_opengl3.cpp"
	}

	filter "system:windows"
		staticruntime "On"
		systemversion "latest"
		defines {  }

	filter "configurations:Debug"
		symbols "On"
		runtime "Debug"

filter "configurations:Release"
	optimize "On"
	runtime "Release"

project "ImGuiColorTextEdit"

	location "Source/ImGuiColorTextEdit/"
	kind "StaticLib"
	language "C++"
	targetdir ("Intermediate/%{prj.name}-" .. outputdir)
	objdir ("Intermediate/%{prj.name}-" .. outputdir)

	includedirs {
		"%{prj.location}/",
		"Source/IMGUI/"
	}

	files {
		"%{prj.location}/TextEditor.h",
		"%{prj.location}/TextEditor.cpp"
	}

	filter "system:windows"
		cppdialect "C++17"
		staticruntime "On"
		systemversion "latest"

	filter "configurations:Debug"
		symbols "On"
		runtime "Debug"

	filter "configurations:Release"
		optimize "On"
		runtime "Release"

-- Lua 5.4 static library (sources to be vendored under Source/LUA/)
project "LUA"

location "Source/LUA/"
kind "StaticLib"
language "C"
targetdir ("Intermediate/%{prj.name}-" .. outputdir)
objdir ("Intermediate/%{prj.name}-" .. outputdir)

includedirs
{
"%{prj.location}/"
}

files
{
"%{prj.location}/**.h",
"%{prj.location}/**.c"
}
-- Exclude Lua sources that define a program entry point (main) or test mains.
removefiles { "%{prj.location}/lua.c", "%{prj.location}/onelua.c", "%{prj.location}/ltests.c", "%{prj.location}/ltests/**.c" }

filter "system:windows"
staticruntime "On"
systemversion "latest"

filter "configurations:Debug"
symbols "On"
runtime "Debug"

filter "configurations:Release"
optimize "On"
runtime "Release"

group ""

project "GOAPie"
	
	location "Source/GOAPie/"
	kind "StaticLib"
	language "C++"
	
	includedirs
	{
		"Source/GLM/",
		"Source/UUID_V4/"
	}

	files
	{
		"%{prj.location}/include/**.h"
	}
	
	filter "system:windows"
		cppdialect "C++20"
		staticruntime "On"
		systemversion "latest"

	filter "configurations:Debug"
		defines "GIE_DEBUG"
		symbols "On"
		runtime "Debug"

	filter "configurations:Release"
		defines "GIE_RELEASE"
		optimize "On"
		runtime "Release"

project "Tests"

location "Source/GOAPie_Tests/"
kind "ConsoleApp"
language "C++"
targetdir ("Binaries/%{prj.name}-" .. outputdir)
objdir ("Intermediate/%{prj.name}-" .. outputdir)

dependson { "GOAPie", "LUA", "ImGuiColorTextEdit" }
defines { "GIE_WITH_LUA=1" }
includedirs
{
"Source/GOAPie/include/",
"Source/GLFW/include/",
"Source/GLAD/include/",
"Source/GLM/",
"Source/IMGUI/",
"Source/IMGUI/backends/",
"Source/UUID_V4/",
"Source/LUA/",
"Source/ImGuiColorTextEdit/"
}

links
{
"GLAD.lib",
"GLFW.lib",
"IMGUI.lib",
"LUA.lib",
"ImGuiColorTextEdit.lib",
"opengl32.lib"
}

libdirs
{
("Intermediate/GLAD-" .. outputdir),
("Intermediate/GLFW-" .. outputdir),
("Intermediate/IMGUI-" .. outputdir),
("Intermediate/LUA-" .. outputdir),
("Intermediate/ImGuiColorTextEdit-" .. outputdir)
}

-- Exclude embedded stub sources so only submodule implementation is built
removefiles {
"%{prj.location}/src/thirdparty/ImGuiColorTextEdit/**.cpp",
"%{prj.location}/src/thirdparty/ImGuiColorTextEdit/**.h"
}

	files
	{
		"%{prj.location}/include/**.h",
		"%{prj.location}/src/**.cpp",
		-- Include lua scripts so they appear in Visual Studio solution explorer
		"%{prj.location}/scripts/**.lua"
	}

	-- Mark lua files as non-build items in Visual Studio (optional)
	filter { "files:%{prj.location}/scripts/**.lua" }
		excludefrombuild "On"

	filter "system:windows"
		cppdialect "C++17"
		staticruntime "On"
		systemversion "latest"
		postbuildcommands {
			'mkdir "$(TargetDir)examples" 2>nul',
			'for /D %%D in ("$(ProjectDir)scripts\\*") do @call "$(ProjectDir)scripts\\copy_lua_examples.bat" "%%~fD" "$(TargetDir)examples"'
		}

	filter "configurations:Debug"
		symbols "On"
		defines { "GIE_DEBUG" }
		runtime "Debug"

	filter "configurations:Release"
		optimize "On"
		runtime "Release"
