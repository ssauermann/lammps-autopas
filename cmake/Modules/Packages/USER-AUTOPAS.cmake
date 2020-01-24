if (PKG_USER-AUTOPAS)

    if (CMAKE_VERSION VERSION_LESS "3.14")
        message(FATAL_ERROR "For the AutoPas package you need at least cmake-3.14")
    endif ()

    enable_language(C)
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)

    ######### 1. Clone and build AutoPas library #########

    # Enable ExternalProject CMake module
    include(ExternalProject)

    # Select https (default) or ssh path.
    set(autopasRepoPath https://github.com/AutoPas/AutoPas.git)
    if (GIT_SUBMODULES_SSH)
        set(autopasRepoPath git@github.com:AutoPas/AutoPas.git)
    endif ()

    # Download and install autopas
    ExternalProject_Add(
            autopas
            GIT_REPOSITORY ${autopasRepoPath}
            # GIT_TAG f639d8b77eb62b84ffb3717ca4a3e25f1caaea86
            GIT_TAG e95e1e61d5a9bbf862ea9100dfdb080778c5f78b
            #GIT_TAG origin/feature/regionParticleIteratorIncrease
            BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR}/autopas/build
            BUILD_BYPRODUCTS ${CMAKE_CURRENT_BINARY_DIR}/autopas/build/src/autopas/libautopas.a
            PREFIX ${CMAKE_CURRENT_BINARY_DIR}/autopas
            # Disable install step
            INSTALL_COMMAND ""
            CMAKE_ARGS
            -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
            -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
            -DAUTOPAS_BUILD_TESTS=OFF
            -DAUTOPAS_BUILD_EXAMPLES=OFF
            -DAUTOPAS_ENABLE_ADDRESS_SANITIZER=${ENABLE_ADDRESS_SANITIZER}
            -DAUTOPAS_OPENMP=${BUILD_OMP}
            -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
            -Dspdlog_ForceBundled=ON
            -DEigen3_ForceBundled=ON
    )

    # Get autopas source and binary directories from CMake project
    ExternalProject_Get_Property(autopas source_dir binary_dir)

    # Create a libautopas target to be used as a dependency by the program
    add_library(libautopas IMPORTED STATIC GLOBAL)
    add_dependencies(libautopas autopas)

    # Using target_compile_definitions for imported targets is only possible starting with cmake 3.11, so we use add_definitions here.
    if (BUILD_OMP)
        add_definitions(-DAUTOPAS_OPENMP)
    endif (BUILD_OMP)

    set_target_properties(libautopas PROPERTIES
            "IMPORTED_LOCATION" "${binary_dir}/src/autopas/libautopas.a"
            "IMPORTED_LINK_INTERFACE_LIBRARIES" "${CMAKE_THREAD_LIBS_INIT}"
            )

    # workaround for INTERFACE_INCLUDE_DIRECTORIES requiring existent paths, so we create them here...
    file(MAKE_DIRECTORY ${source_dir}/src)
    file(MAKE_DIRECTORY ${binary_dir}/libs/spdlog/src/spdlog_bundled/include)
    file(MAKE_DIRECTORY ${binary_dir}/libs/eigen-3/include)

    target_include_directories(libautopas SYSTEM INTERFACE
            "${source_dir}/src"
            "${binary_dir}/libs/spdlog/src/spdlog_bundled/include"
            "${binary_dir}/libs/eigen-3/include"
            )

    list(APPEND LAMMPS_LINK_LIBS "libautopas")


    ######### 2. AutoPas package settings #########

    set(USER-AUTOPAS_SOURCES_DIR ${LAMMPS_SOURCE_DIR}/USER-AUTOPAS)

    # Sources that are not a style
    set(USER-AUTOPAS_SOURCES #)${USER-AUTOPAS_SOURCES_DIR}/fix_autopas.cpp
            # ${USER-AUTOPAS_SOURCES_DIR}/thr_data.cpp
            # ${USER-AUTOPAS_SOURCES_DIR}/thr_omp.cpp
            )

    set_property(GLOBAL PROPERTY "AUTOPAS_SOURCES" "${USER-AUTOPAS_SOURCES}")

    # detects styles which have USER-AUTOPAS version
    RegisterStylesExt(${USER-AUTOPAS_SOURCES_DIR} autopas AUTOPAS_SOURCES)

    get_property(USER-AUTOPAS_SOURCES GLOBAL PROPERTY AUTOPAS_SOURCES)

    list(APPEND LIB_SOURCES ${USER-AUTOPAS_SOURCES})
    include_directories(${USER-AUTOPAS_SOURCES_DIR})
endif ()