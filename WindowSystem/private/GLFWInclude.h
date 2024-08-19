#pragma once
#if defined(_WIN32) || defined(_WIN64)
#define GLFW_EXPOSE_NATIVE_WIN32 1
#endif
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>