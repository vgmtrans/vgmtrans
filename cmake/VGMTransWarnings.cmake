# VGMTrans (c) 2002-2026
# Licensed under the zlib license
# Check the included LICENSE.txt for details

include_guard(GLOBAL)

option(VGMTRANS_WARNINGS_AS_ERRORS "Treat VGMTrans warnings as errors" OFF)

function(vgmtrans_enable_project_warnings target)
  if(MSVC OR CMAKE_CXX_SIMULATE_ID STREQUAL "MSVC")
    target_compile_options(
      ${target}
      PRIVATE
        $<$<COMPILE_LANGUAGE:C>:/W4>
        $<$<COMPILE_LANGUAGE:C>:/wd4100>
        $<$<COMPILE_LANGUAGE:C>:/wd4146>
        $<$<COMPILE_LANGUAGE:C>:/wd4244>
        $<$<COMPILE_LANGUAGE:C>:/wd4245>
        $<$<COMPILE_LANGUAGE:C>:/wd4267>
        $<$<COMPILE_LANGUAGE:C>:/wd4305>
        $<$<COMPILE_LANGUAGE:C>:/wd4458>
        $<$<COMPILE_LANGUAGE:C>:/wd4701>
        $<$<COMPILE_LANGUAGE:C>:/wd4702>
        $<$<COMPILE_LANGUAGE:CXX>:/W4>
        $<$<COMPILE_LANGUAGE:CXX>:/wd4100>
        $<$<COMPILE_LANGUAGE:CXX>:/wd4146>
        $<$<COMPILE_LANGUAGE:CXX>:/wd4244>
        $<$<COMPILE_LANGUAGE:CXX>:/wd4245>
        $<$<COMPILE_LANGUAGE:CXX>:/wd4267>
        $<$<COMPILE_LANGUAGE:CXX>:/wd4305>
        $<$<COMPILE_LANGUAGE:CXX>:/wd4458>
        $<$<COMPILE_LANGUAGE:CXX>:/wd4701>
        $<$<COMPILE_LANGUAGE:CXX>:/wd4702>
        $<$<COMPILE_LANG_AND_ID:CXX,Clang>:-Wno-unused-const-variable>
        $<$<COMPILE_LANG_AND_ID:CXX,Clang>:-Wno-unused-private-field>
        $<$<COMPILE_LANG_AND_ID:CXX,Clang>:-Wdeprecated-copy-with-dtor>
    )
  else()
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
        $<$<COMPILE_LANG_AND_ID:CXX,Clang,AppleClang>:-Wdeprecated-copy-with-dtor>
    )
  endif()

  if(VGMTRANS_WARNINGS_AS_ERRORS)
    set_property(TARGET ${target} PROPERTY COMPILE_WARNING_AS_ERROR ON)
  endif()
endfunction()
