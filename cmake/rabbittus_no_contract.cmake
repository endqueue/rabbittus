# rabbittus_no_contract(TARGETS <target>... SOURCES <source>...)
#
# Turns floating-point contraction off for the targets and sources named:
# what Rabbittus Math, Rabbittus Text and Rend's lit CPU code compute must
# be the same bits on every platform, and a fused multiply-add rounds once
# where the written code rounds twice (card A63, decided 2026-11-17;
# RBTMATH_TODO.md). The flag is chosen by the compiler's ID, never by
# "MSVC", which CMake also sets for clang-cl, a compiler that contracts by
# default:
#   - clang-cl (Clang with MSVC's front end): /clang:-ffp-contract=off
#   - any other Clang (Apple's included) and GCC: -ffp-contract=off
#   - MSVC: /fp:precise, from Visual Studio 2022 17.0 (cl 19.30), which
#     contracts only under /fp:contract or /fp:fast; earlier versions
#     contracted under /fp:precise, so they are refused
#   - anything else: refused, since nothing says how it contracts.
# Each language's flag is chosen from its own compiler. A source's flag is
# appended to its COMPILE_OPTIONS, so a source may carry others too; a
# target's goes in PRIVATE.

function(_rabbittus_no_contract_flag lang out_var)
    set(id "${CMAKE_${lang}_COMPILER_ID}")
    if (id MATCHES "Clang" AND CMAKE_${lang}_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
        set(flag /clang:-ffp-contract=off)
    elseif (id MATCHES "Clang|GNU")
        set(flag -ffp-contract=off)
    elseif (id STREQUAL "MSVC")
        if (CMAKE_${lang}_COMPILER_VERSION VERSION_LESS 19.30)
            message(FATAL_ERROR "rabbittus: ${lang} compiler cl ${CMAKE_${lang}_COMPILER_VERSION} contracts under /fp:precise; Visual Studio 2022 17.0 (cl 19.30) or later is needed")
        endif ()
        set(flag /fp:precise)
    else ()
        message(FATAL_ERROR "rabbittus: no known way to turn floating-point contraction off for the ${lang} compiler ${id}")
    endif ()
    set(${out_var} ${flag} PARENT_SCOPE)
endfunction()

function(rabbittus_no_contract)
    cmake_parse_arguments(PARSE_ARGV 0 ARG "" "" "TARGETS;SOURCES")
    if (ARG_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "rabbittus_no_contract: unknown arguments ${ARG_UNPARSED_ARGUMENTS}")
    endif ()
    get_property(languages GLOBAL PROPERTY ENABLED_LANGUAGES)
    set(flag_C "")
    set(flag_CXX "")
    foreach (lang C CXX)
        if (lang IN_LIST languages)
            _rabbittus_no_contract_flag(${lang} flag_${lang})
        endif ()
    endforeach ()
    foreach (target IN LISTS ARG_TARGETS)
        if (flag_C STREQUAL flag_CXX OR flag_C STREQUAL "")
            target_compile_options(${target} PRIVATE ${flag_CXX})
        else ()
            target_compile_options(${target} PRIVATE "$<$<COMPILE_LANGUAGE:C>:${flag_C}>" "$<$<COMPILE_LANGUAGE:CXX>:${flag_CXX}>")
        endif ()
    endforeach ()
    foreach (source IN LISTS ARG_SOURCES)
        get_filename_component(extension "${source}" LAST_EXT)
        if (extension STREQUAL ".c")
            set(flag ${flag_C})
        else ()
            set(flag ${flag_CXX})
        endif ()
        set_property(SOURCE "${source}" APPEND PROPERTY COMPILE_OPTIONS ${flag})
    endforeach ()
endfunction()
