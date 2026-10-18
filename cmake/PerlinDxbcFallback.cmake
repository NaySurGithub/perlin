# Compiles one shader for Direct3D 11 (s_5_0), trying simpler variants if FXC crashes.
#
# FXC (d3dcompiler_47, Microsoft's closed-source D3D11 compiler) overflows its stack on the full
# Vibrant Visuals water shader. This script tries the full shader first, then variants with
# features compiled out, uses the first one that compiles and prints which variants failed, so
# the build always works and the log shows what FXC chokes on.
#
# cmake -DSHADERC=... -DSRC=... -DOUT=... -DVARYING=... -DNAME=... -DINCLUDES="a|b|c"
#       -DVARIANTS="full|DEF_A|DEF_A,DEF_B" -P PerlinDxbcFallback.cmake
foreach(v SHADERC SRC OUT VARYING NAME INCLUDES VARIANTS)
    if(NOT DEFINED ${v})
        message(FATAL_ERROR "PerlinDxbcFallback: ${v} not set")
    endif()
endforeach()

string(REPLACE "|" ";" include_list "${INCLUDES}")
set(include_args)
foreach(dir IN LISTS include_list)
    list(APPEND include_args -i "${dir}")
endforeach()

string(REPLACE "|" ";" variant_list "${VARIANTS}")
set(failed)
foreach(variant IN LISTS variant_list)
    set(define_args)
    if(NOT variant STREQUAL "full")
        string(REPLACE "," ";" defines "${variant}")
        list(APPEND define_args --define "${defines}")
    endif()
    execute_process(
        COMMAND "${SHADERC}" -f "${SRC}" ${include_args} -o "${OUT}.tmp" --bin2c "${NAME}"
                --platform windows --type fragment --varyingdef "${VARYING}" --profile s_5_0 -O 3 --Werror
                ${define_args}
        RESULT_VARIABLE rc
        OUTPUT_VARIABLE out
        ERROR_VARIABLE err)
    if(rc EQUAL 0 AND EXISTS "${OUT}.tmp")
        file(RENAME "${OUT}.tmp" "${OUT}")
        if(failed)
            message(WARNING "[dxbc] ${NAME}: FXC failed on variant(s) [${failed}]; using variant [${variant}]")
        else()
            message(STATUS "[dxbc] ${NAME}: full shader compiled")
        endif()
        return()
    endif()
    message(STATUS "[dxbc] ${NAME}: variant [${variant}] failed (exit ${rc}) ${out} ${err}")
    list(APPEND failed "${variant}")
    file(REMOVE "${OUT}.tmp")
endforeach()
message(FATAL_ERROR "[dxbc] ${NAME}: every variant failed: [${failed}]")
