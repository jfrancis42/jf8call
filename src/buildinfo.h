#pragma once

// Seconds since the Unix epoch at the time this binary was compiled.
// Set by cmake/write_build_time.cmake → build/build_timestamp.h.
extern const long long g_buildTime;
