# VGMTrans (c) 2002-2026
# Licensed under the zlib license
# Check the included LICENSE.txt for details

include_guard(GLOBAL)

option(VGMTRANS_WARNINGS_AS_ERRORS "Treat VGMTrans warnings as errors" OFF)

function(vgmtrans_enable_project_warnings target)
  target_compile_options(
    ${target}
    PRIVATE
      $<$<COMPILE_LANG_AND_ID:C,GNU,Clang,AppleClang>:-Wall>
      $<$<COMPILE_LANG_AND_ID:C,GNU,Clang,AppleClang>:-Wextra>
      $<$<COMPILE_LANG_AND_ID:C,GNU,Clang,AppleClang>:-Wno-unused-parameter>
      $<$<COMPILE_LANG_AND_ID:C,GNU,Clang,AppleClang>:-Wcast-align>
      $<$<COMPILE_LANG_AND_ID:C,GNU,Clang,AppleClang>:-Wnull-dereference>
      $<$<COMPILE_LANG_AND_ID:C,GNU>:-Wshadow=local>
      $<$<COMPILE_LANG_AND_ID:C,Clang,AppleClang>:-Wshadow>

      $<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang,AppleClang>:-Wall>
      $<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang,AppleClang>:-Wextra>
      $<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang,AppleClang>:-Wno-unused-parameter>
      $<$<COMPILE_LANG_AND_ID:CXX,Clang,AppleClang>:-Wno-c++98-compat-pedantic>
      $<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang,AppleClang>:-Wcast-align>
      $<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang,AppleClang>:-Wnull-dereference>
      $<$<COMPILE_LANG_AND_ID:CXX,GNU>:-Wshadow=local>
      $<$<COMPILE_LANG_AND_ID:CXX,Clang,AppleClang>:-Wshadow>
      $<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang,AppleClang>:-Woverloaded-virtual>

      $<$<COMPILE_LANG_AND_ID:C,MSVC>:/W4>
      $<$<COMPILE_LANG_AND_ID:C,MSVC>:/wd4100>
      $<$<COMPILE_LANG_AND_ID:C,MSVC>:/wd4458>
      $<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/W4>
      $<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/wd4100>
      $<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/wd4æ58>
  )

  if(VGMTRANS_WARNINGS_AS_ERRORS)
    set_property(TARGET ${target} PROPERTY COMPILE_WARNING_AS_ERROR ON)
  endif()
endfunction()
