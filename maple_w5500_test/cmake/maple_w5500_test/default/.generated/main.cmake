include("${CMAKE_CURRENT_LIST_DIR}/rule.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/file.cmake")

set(maple_w5500_test_default_library_list )

# Handle files with suffix (s|as|asm|AS|ASM|As|aS|Asm), for group default-XC8
if(maple_w5500_test_default_default_XC8_FILE_TYPE_assemble)
add_library(maple_w5500_test_default_default_XC8_assemble OBJECT ${maple_w5500_test_default_default_XC8_FILE_TYPE_assemble})
    maple_w5500_test_default_default_XC8_assemble_rule(maple_w5500_test_default_default_XC8_assemble)
    list(APPEND maple_w5500_test_default_library_list "$<TARGET_OBJECTS:maple_w5500_test_default_default_XC8_assemble>")

endif()

# Handle files with suffix S, for group default-XC8
if(maple_w5500_test_default_default_XC8_FILE_TYPE_assemblePreprocess)
add_library(maple_w5500_test_default_default_XC8_assemblePreprocess OBJECT ${maple_w5500_test_default_default_XC8_FILE_TYPE_assemblePreprocess})
    maple_w5500_test_default_default_XC8_assemblePreprocess_rule(maple_w5500_test_default_default_XC8_assemblePreprocess)
    list(APPEND maple_w5500_test_default_library_list "$<TARGET_OBJECTS:maple_w5500_test_default_default_XC8_assemblePreprocess>")

endif()

# Handle files with suffix [cC], for group default-XC8
if(maple_w5500_test_default_default_XC8_FILE_TYPE_compile)
add_library(maple_w5500_test_default_default_XC8_compile OBJECT ${maple_w5500_test_default_default_XC8_FILE_TYPE_compile})
    maple_w5500_test_default_default_XC8_compile_rule(maple_w5500_test_default_default_XC8_compile)
    list(APPEND maple_w5500_test_default_library_list "$<TARGET_OBJECTS:maple_w5500_test_default_default_XC8_compile>")

endif()

# Handle files with suffix elf, for group default-XC8
if(maple_w5500_test_default_default_XC8_FILE_TYPE_objcopy_lss)
add_library(maple_w5500_test_default_default_XC8_objcopy_lss OBJECT ${maple_w5500_test_default_default_XC8_FILE_TYPE_objcopy_lss})
    maple_w5500_test_default_default_XC8_objcopy_lss_rule(maple_w5500_test_default_default_XC8_objcopy_lss)
    list(APPEND maple_w5500_test_default_library_list "$<TARGET_OBJECTS:maple_w5500_test_default_default_XC8_objcopy_lss>")

endif()


# Main target for this project
add_executable(maple_w5500_test_default_image_vixPuhmv ${maple_w5500_test_default_library_list})

set_target_properties(maple_w5500_test_default_image_vixPuhmv PROPERTIES
    OUTPUT_NAME "default"
    SUFFIX ".elf"
    ADDITIONAL_CLEAN_FILES "${output_extensions}"
    RUNTIME_OUTPUT_DIRECTORY "${maple_w5500_test_default_output_dir}")
target_link_libraries(maple_w5500_test_default_image_vixPuhmv PRIVATE ${maple_w5500_test_default_default_XC8_FILE_TYPE_link})
# Add the link options from the rule file.
maple_w5500_test_default_link_rule( maple_w5500_test_default_image_vixPuhmv)


#Add objcopy steps
maple_w5500_test_default_objcopy_lss_rule(maple_w5500_test_default_image_vixPuhmv)

