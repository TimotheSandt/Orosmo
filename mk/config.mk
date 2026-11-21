# Engine identity — single source of truth for the build system and the engine tool binary.
# Commit this file.  Values here are baked into the engine[.exe] binary at compile time.

ENGINE_NAME        ?= Orosmo
ENGINE_VERSION     ?= 0.3.1
ENGINE_PUBLISHER   ?= Gasshog
ENGINE_AUTHOR      ?= Timothé Sandt
ENGINE_EMAIL       ?= timothe.sandt@proton.me
ENGINE_DESCRIPTION ?= Orosmo: a C++23 3D/2D game engine (RHI with OpenGL 4.3+, Vulkan and Metal planned) with a precompiled UI language and JSON themes.
ENGINE_HOMEPAGE    ?= https://github.com/TimotheSandt/Orosmo
