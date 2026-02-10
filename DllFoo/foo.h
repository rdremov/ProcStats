#pragma once

#ifdef DLLFOO_EXPORTS
  #define DLLFOO_API __declspec(dllexport)
#else
  #define DLLFOO_API __declspec(dllimport)
#endif

DLLFOO_API void foo();