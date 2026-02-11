# Post-build helpers for copying CEF runtime files to output directory

function(copy_cef_runtime_files target)
    # Copy libcef.dll and other required DLLs
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CEF_LIB_DIR}/libcef.dll"
            "$<TARGET_FILE_DIR:${target}>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CEF_LIB_DIR}/chrome_elf.dll"
            "$<TARGET_FILE_DIR:${target}>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CEF_LIB_DIR}/v8_context_snapshot.bin"
            "$<TARGET_FILE_DIR:${target}>"
        COMMENT "Copying CEF runtime DLLs..."
    )

    # Copy resource files
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CEF_RESOURCE_DIR}/icudtl.dat"
            "$<TARGET_FILE_DIR:${target}>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CEF_RESOURCE_DIR}/chrome_100_percent.pak"
            "$<TARGET_FILE_DIR:${target}>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CEF_RESOURCE_DIR}/chrome_200_percent.pak"
            "$<TARGET_FILE_DIR:${target}>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CEF_RESOURCE_DIR}/resources.pak"
            "$<TARGET_FILE_DIR:${target}>"
        COMMENT "Copying CEF resource files..."
    )

    # Copy locales directory
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CEF_RESOURCE_DIR}/locales"
            "$<TARGET_FILE_DIR:${target}>/locales"
        COMMENT "Copying CEF locales..."
    )
endfunction()
