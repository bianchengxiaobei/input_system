set(INSYS_WEBVIEW2_VERSION "1.0.3537.50" CACHE STRING
    "Microsoft.Web.WebView2 NuGet package version")
set(INSYS_WEBVIEW2_ROOT "" CACHE PATH
    "Extracted Microsoft.Web.WebView2 package root (contains build/native)")

function(insys_require_webview2 out_include out_loader)
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(webview2_arch x64)
    else()
        set(webview2_arch x86)
    endif()

    set(root "${INSYS_WEBVIEW2_ROOT}")
    if(NOT root)
        find_path(webview2_include_hint
            NAMES WebView2.h
            HINTS
                "$ENV{USERPROFILE}/.nuget/packages/microsoft.web.webview2/${INSYS_WEBVIEW2_VERSION}/build/native/include"
            NO_DEFAULT_PATH)
        if(webview2_include_hint)
            get_filename_component(root "${webview2_include_hint}/../../.." ABSOLUTE)
        endif()
    endif()

    if(NOT root)
        set(package_dir
            "${CMAKE_BINARY_DIR}/_deps/webview2-${INSYS_WEBVIEW2_VERSION}")
        set(package_file "${package_dir}.nupkg")
        if(NOT EXISTS "${package_file}")
            set(download_file "${package_file}.tmp")
            file(DOWNLOAD
                "https://api.nuget.org/v3-flatcontainer/microsoft.web.webview2/${INSYS_WEBVIEW2_VERSION}/microsoft.web.webview2.${INSYS_WEBVIEW2_VERSION}.nupkg"
                "${download_file}"
                EXPECTED_HASH
                    SHA256=5EA526BBD728ADDA0DA4D31219267E96460494A427E4894C4E09D9F320F4B9AA
                STATUS download_status
                LOG download_log)
            list(GET download_status 0 download_code)
            if(NOT download_code EQUAL 0)
                file(REMOVE "${download_file}")
                message(FATAL_ERROR
                    "Could not download Microsoft.Web.WebView2 ${INSYS_WEBVIEW2_VERSION}. "
                    "Set INSYS_WEBVIEW2_ROOT to an extracted package or "
                    "INSYS_BUILD_EDITOR_SHELL=OFF.\n${download_log}")
            endif()
            file(RENAME "${download_file}" "${package_file}")
        endif()

        file(ARCHIVE_EXTRACT
            INPUT "${package_file}"
            DESTINATION "${package_dir}")
        if(NOT EXISTS "${package_dir}/build/native/include/WebView2.h")
            message(FATAL_ERROR
                "WebView2 package extraction did not produce WebView2.h in "
                "${package_dir}")
        endif()
        set(root "${package_dir}")
    endif()

    set(include_dir "${root}/build/native/include")
    set(loader_lib "${root}/build/native/${webview2_arch}/WebView2LoaderStatic.lib")
    if(NOT EXISTS "${include_dir}/WebView2.h")
        message(FATAL_ERROR
            "INSYS_WEBVIEW2_ROOT does not contain build/native/include/WebView2.h: "
            "${root}")
    endif()
    if(NOT EXISTS "${loader_lib}")
        message(FATAL_ERROR
            "WebView2 static loader not found for ${webview2_arch}: ${loader_lib}")
    endif()

    set(${out_include} "${include_dir}" PARENT_SCOPE)
    set(${out_loader} "${loader_lib}" PARENT_SCOPE)
    message(STATUS "WebView2 ${INSYS_WEBVIEW2_VERSION}: ${root}")
endfunction()
