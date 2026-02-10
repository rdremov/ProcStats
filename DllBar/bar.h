#pragma once

#ifdef DLLBAR_EXPORTS
  #define DLLBAR_API __declspec(dllexport)
#else
  #define DLLBAR_API __declspec(dllimport)
#endif

DLLBAR_API void bar();
