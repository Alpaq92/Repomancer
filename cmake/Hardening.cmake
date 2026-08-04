# SPDX-License-Identifier: Apache-2.0
# Compiler/linker hardening applied to every first-party target
# (implementation-plan.md §13.2). Ship-blocker: artifacts must carry these.

function(repomancer_harden target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /GS /guard:cf /sdl)
    target_link_options(${target} PRIVATE
      /DYNAMICBASE /HIGHENTROPYVA /guard:cf)
    # CETCOMPAT is safe on x64; ARM64 links reject it.
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "AMD64|x86_64")
      target_link_options(${target} PRIVATE /CETCOMPAT)
    endif()
  else()
    target_compile_options(${target} PRIVATE -fstack-protector-strong)
    # _FORTIFY_SOURCE needs optimization; glibc warns at -O0.
    target_compile_definitions(${target} PRIVATE
      $<$<NOT:$<CONFIG:Debug>>:_FORTIFY_SOURCE=3>)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
      target_compile_options(${target} PRIVATE -fPIE)
      target_link_options(${target} PRIVATE
        -Wl,-z,relro -Wl,-z,now)
      get_target_property(_type ${target} TYPE)
      if(_type STREQUAL "EXECUTABLE")
        target_link_options(${target} PRIVATE -pie)
      endif()
    endif()
  endif()
endfunction()
