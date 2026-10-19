# The following functions contains all the flags passed to the different build stages.

set(PACK_REPO_PATH "C:/Users/John Christie/.mchp_packs" CACHE PATH "Path to the root of a pack repository.")

function(maple_w5500_test_default_default_XC8_assemble_rule target)
    set(options
        "-c"
        "${MP_EXTRA_AS_PRE}"
        "-mcpu=18F47K42"
        "-mdfp=${PACK_REPO_PATH}/Microchip/PIC18F-K_DFP/1.17.312/xc8"
        "-fno-short-double"
        "-fno-short-float"
        "-memi=wordwrite"
        "-O0"
        "-maddrqual=ignore"
        "-mwarn=-3"
        "-msummary=-psect,-class,+mem,-hex,-file"
        "-ginhx32"
        "-Wl,--data-init"
        "-mno-keep-startup"
        "-mno-download"
        "-mno-default-config-bits"
        "-std=c99"
        "-gdwarf-3"
        "-mstack=compiled:auto:auto:auto")
    list(REMOVE_ITEM options "")
    target_compile_options(${target} PRIVATE "${options}")
    target_compile_definitions(${target}
        PRIVATE "__18F47K42__"
        PRIVATE "XPRJ_default=default")
endfunction()
function(maple_w5500_test_default_default_XC8_assemblePreprocess_rule target)
    set(options
        "-c"
        "${MP_EXTRA_AS_PRE}"
        "-mcpu=18F47K42"
        "-x"
        "assembler-with-cpp"
        "-mdfp=${PACK_REPO_PATH}/Microchip/PIC18F-K_DFP/1.17.312/xc8"
        "-fno-short-double"
        "-fno-short-float"
        "-memi=wordwrite"
        "-O0"
        "-maddrqual=ignore"
        "-mwarn=-3"
        "-msummary=-psect,-class,+mem,-hex,-file"
        "-ginhx32"
        "-Wl,--data-init"
        "-mno-keep-startup"
        "-mno-download"
        "-mno-default-config-bits"
        "-std=c99"
        "-gdwarf-3"
        "-mstack=compiled:auto:auto:auto")
    list(REMOVE_ITEM options "")
    target_compile_options(${target} PRIVATE "${options}")
    target_compile_definitions(${target}
        PRIVATE "__18F47K42__"
        PRIVATE "XPRJ_default=default")
endfunction()
function(maple_w5500_test_default_default_XC8_compile_rule target)
    set(options
        "-c"
        "${MP_EXTRA_CC_PRE}"
        "-mcpu=18F47K42"
        "-mdfp=${PACK_REPO_PATH}/Microchip/PIC18F-K_DFP/1.17.312/xc8"
        "-fno-short-double"
        "-fno-short-float"
        "-memi=wordwrite"
        "-O0"
        "-maddrqual=ignore"
        "-mwarn=-3"
        "-msummary=-psect,-class,+mem,-hex,-file"
        "-ginhx32"
        "-Wl,--data-init"
        "-mno-keep-startup"
        "-mno-download"
        "-mno-default-config-bits"
        "-std=c99"
        "-gdwarf-3"
        "-mstack=compiled:auto:auto:auto")
    list(REMOVE_ITEM options "")
    target_compile_options(${target} PRIVATE "${options}")
    target_compile_definitions(${target}
        PRIVATE "__18F47K42__"
        PRIVATE "XPRJ_default=default")
endfunction()
function(maple_w5500_test_default_link_rule target)
    set(options
        "-Wl,-Map=mem.map"
        "${MP_EXTRA_LD_PRE}"
        "-mcpu=18F47K42"
        "-Wl,--defsym=__MPLAB_BUILD=1"
        "-mdfp=${PACK_REPO_PATH}/Microchip/PIC18F-K_DFP/1.17.312/xc8"
        "-fno-short-double"
        "-fno-short-float"
        "-memi=wordwrite"
        "-O0"
        "-maddrqual=ignore"
        "-mwarn=-3"
        "-msummary=-psect,-class,+mem,-hex,-file"
        "-ginhx32"
        "-Wl,--data-init"
        "-mno-keep-startup"
        "-mno-download"
        "-mno-default-config-bits"
        "-std=c99"
        "-gdwarf-3"
        "-mstack=compiled:auto:auto:auto"
        "-Wl,--memorysummary,memoryfile.xml")
    list(REMOVE_ITEM options "")
    target_link_options(${target} PRIVATE "${options}")
    target_compile_definitions(${target} PRIVATE "XPRJ_default=default")
endfunction()
function(maple_w5500_test_default_objcopy_lss_rule target)
    add_custom_command(
        TARGET ${target}
        POST_BUILD
        COMMAND ${OBJDUMP}
        ARGS --disassemble --wide --demangle --line-numbers --section-headers --source ${maple_w5500_test_default_image_name} > ${maple_w5500_test_default_image_base_name}.lss
        WORKING_DIRECTORY ${maple_w5500_test_default_output_dir})
endfunction()
