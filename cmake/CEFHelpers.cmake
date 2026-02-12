# Post-build helpers for copying CEF runtime files to output directory
# All CEF files go into a nexus_js_loader/ subfolder to avoid polluting
# the addon root and conflicting with GW2's own libcef.dll.

function(copy_cef_runtime_files target)
    set(CEF_SUBDIR "$<TARGET_FILE_DIR:${target}>/nexus_js_loader")

    # Create subfolder
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CEF_SUBDIR}"
        COMMENT "Creating CEF subfolder..."
    )

    # Copy libcef.dll and other required DLLs
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CEF_LIB_DIR}/libcef.dll"
            "${CEF_SUBDIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CEF_LIB_DIR}/chrome_elf.dll"
            "${CEF_SUBDIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CEF_LIB_DIR}/snapshot_blob.bin"
            "${CEF_SUBDIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CEF_LIB_DIR}/v8_context_snapshot.bin"
            "${CEF_SUBDIR}"
        COMMENT "Copying CEF runtime DLLs..."
    )

    # Copy bootstrap binaries when present (newer CEF Windows builds).
    foreach(bootstrap_exe bootstrap.exe bootstrapc.exe)
        if(EXISTS "${CEF_LIB_DIR}/${bootstrap_exe}")
            add_custom_command(TARGET ${target} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${CEF_LIB_DIR}/${bootstrap_exe}"
                    "${CEF_SUBDIR}"
            )
        endif()
    endforeach()

    # Copy GPU/rendering DLLs
    foreach(gpu_dll d3dcompiler_47.dll libEGL.dll libGLESv2.dll
                    vk_swiftshader.dll vulkan-1.dll dxcompiler.dll dxil.dll)
        if(EXISTS "${CEF_LIB_DIR}/${gpu_dll}")
            add_custom_command(TARGET ${target} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${CEF_LIB_DIR}/${gpu_dll}"
                    "${CEF_SUBDIR}"
            )
        endif()
    endforeach()

    # Copy vk_swiftshader_icd.json if present
    if(EXISTS "${CEF_LIB_DIR}/vk_swiftshader_icd.json")
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CEF_LIB_DIR}/vk_swiftshader_icd.json"
                "${CEF_SUBDIR}"
        )
    endif()

    # Copy resource files
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CEF_RESOURCE_DIR}/icudtl.dat"
            "${CEF_SUBDIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CEF_RESOURCE_DIR}/chrome_100_percent.pak"
            "${CEF_SUBDIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CEF_RESOURCE_DIR}/chrome_200_percent.pak"
            "${CEF_SUBDIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CEF_RESOURCE_DIR}/resources.pak"
            "${CEF_SUBDIR}"
        COMMENT "Copying CEF resource files..."
    )

    # Copy locales directory
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CEF_RESOURCE_DIR}/locales"
            "${CEF_SUBDIR}/locales"
        COMMENT "Copying CEF locales..."
    )
endfunction()
