#pragma once
// BUILD_LABEL: "<commit>[+], <commit date>" from inject_build_info.py, or ""
// when built without git (source tarball). Include only where
// it is printed, so a new commit doesn't recompile everything.
#if __has_include("build_info.h")
#include "build_info.h"
#endif
#ifndef BUILD_LABEL
#define BUILD_LABEL ""
#endif
