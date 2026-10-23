include("${CMAKE_CURRENT_LIST_DIR}/rule.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/file.cmake")

set(Wavelight_firmware_default_library_list )

# Handle files with suffix (s|as|asm|AS|ASM|As|aS|Asm), for group default-XC8
if(Wavelight_firmware_default_default_XC8_FILE_TYPE_assemble)
add_library(Wavelight_firmware_default_default_XC8_assemble OBJECT ${Wavelight_firmware_default_default_XC8_FILE_TYPE_assemble})
    Wavelight_firmware_default_default_XC8_assemble_rule(Wavelight_firmware_default_default_XC8_assemble)
    list(APPEND Wavelight_firmware_default_library_list "$<TARGET_OBJECTS:Wavelight_firmware_default_default_XC8_assemble>")

endif()

# Handle files with suffix S, for group default-XC8
if(Wavelight_firmware_default_default_XC8_FILE_TYPE_assemblePreprocess)
add_library(Wavelight_firmware_default_default_XC8_assemblePreprocess OBJECT ${Wavelight_firmware_default_default_XC8_FILE_TYPE_assemblePreprocess})
    Wavelight_firmware_default_default_XC8_assemblePreprocess_rule(Wavelight_firmware_default_default_XC8_assemblePreprocess)
    list(APPEND Wavelight_firmware_default_library_list "$<TARGET_OBJECTS:Wavelight_firmware_default_default_XC8_assemblePreprocess>")

endif()

# Handle files with suffix [cC], for group default-XC8
if(Wavelight_firmware_default_default_XC8_FILE_TYPE_compile)
add_library(Wavelight_firmware_default_default_XC8_compile OBJECT ${Wavelight_firmware_default_default_XC8_FILE_TYPE_compile})
    Wavelight_firmware_default_default_XC8_compile_rule(Wavelight_firmware_default_default_XC8_compile)
    list(APPEND Wavelight_firmware_default_library_list "$<TARGET_OBJECTS:Wavelight_firmware_default_default_XC8_compile>")

endif()

# Handle files with suffix elf, for group default-XC8
if(Wavelight_firmware_default_default_XC8_FILE_TYPE_objcopy_avr)
add_library(Wavelight_firmware_default_default_XC8_objcopy_avr OBJECT ${Wavelight_firmware_default_default_XC8_FILE_TYPE_objcopy_avr})
    Wavelight_firmware_default_default_XC8_objcopy_avr_rule(Wavelight_firmware_default_default_XC8_objcopy_avr)
    list(APPEND Wavelight_firmware_default_library_list "$<TARGET_OBJECTS:Wavelight_firmware_default_default_XC8_objcopy_avr>")

endif()


# Main target for this project
add_executable(Wavelight_firmware_default_image_E5NgOjff ${Wavelight_firmware_default_library_list})

set_target_properties(Wavelight_firmware_default_image_E5NgOjff PROPERTIES
    OUTPUT_NAME "default"
    SUFFIX ".elf"
    ADDITIONAL_CLEAN_FILES "${output_extensions}"
    RUNTIME_OUTPUT_DIRECTORY "${Wavelight_firmware_default_output_dir}")
target_link_libraries(Wavelight_firmware_default_image_E5NgOjff PRIVATE ${Wavelight_firmware_default_default_XC8_FILE_TYPE_link})
# Add the link options from the rule file.
Wavelight_firmware_default_link_rule( Wavelight_firmware_default_image_E5NgOjff)


#Add objcopy steps
Wavelight_firmware_default_objcopy_avr_rule(Wavelight_firmware_default_image_E5NgOjff)

