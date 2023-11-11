include(FindGit)
find_package(Git)

if(NOT Git_FOUND)
    message(FATAL_ERROR "Git Not Found!")
else()
    message(STATUS "Git Found!")
endif()

include (ExternalProject)
include(FetchContent)