cmake_minimum_required(VERSION 3.21)
project(AFModConstructor LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)

add_library(AzModCore STATIC
    ../src/Core/ForgeText.cpp
    ../src/Zip/Crc32.cpp
    ../src/Zip/Inflate.cpp
    ../src/Zip/ZipArchive.cpp
    ../src/Zip/ZipWriter.cpp
    core/Zip.cpp
    core/SharedConfig.cpp
    core/Localization.cpp
    core/GameLauncher.cpp
    core/ModPackages.cpp
    core/ModCatalog.cpp
    core/ModLogic.cpp
    core/ModCodegen.cpp
    core/ModRuntime.cpp
    core/ModCompiler.cpp
    core/ModProject.cpp
)

target_include_directories(AzModCore PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/core
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)

if(MSVC)
    target_compile_options(AzModCore PRIVATE /utf-8 /W3)
else()
    target_compile_options(AzModCore PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

add_executable(AFModConstructor WIN32
    app/main.cpp
    app/Theme.cpp
    app/Theme.h
    app/CppHighlighter.cpp
    app/CppHighlighter.h
    app/IconCache.cpp
    app/IconCache.h
    app/UiFormat.cpp
    app/UiFormat.h
    app/EventSheetView.cpp
    app/EventSheetView.h
    app/InspectorDock.cpp
    app/InspectorDock.h
    app/CatalogPickerDialog.cpp
    app/CatalogPickerDialog.h
    app/ExprPickerDialog.cpp
    app/ExprPickerDialog.h
    app/Dialogs.cpp
    app/Dialogs.h
    app/BuildWorker.cpp
    app/BuildWorker.h
    app/MainWindow.cpp
    app/MainWindow.h
)

target_link_libraries(AFModConstructor PRIVATE
    AzModCore
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
)

target_include_directories(AFModConstructor PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/app
)

if(MSVC)
    target_compile_options(AFModConstructor PRIVATE /utf-8 /W3)
else()
    target_compile_options(AFModConstructor PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

add_custom_command(TARGET AFModConstructor POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts $<TARGET_FILE_DIR:AFModConstructor>/fonts
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/assets/lang $<TARGET_FILE_DIR:AFModConstructor>/lang
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/assets/themes $<TARGET_FILE_DIR:AFModConstructor>/themes
    COMMENT "Copying runtime assets"
)

# Copy AmodPack.exe from parent AzForge build or local assets/tools if present
set(AMODPACK_CANDIDATE "")
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../Release/AmodPack.exe")
    set(AMODPACK_CANDIDATE "${CMAKE_CURRENT_SOURCE_DIR}/../Release/AmodPack.exe")
elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/assets/tools/AmodPack.exe")
    set(AMODPACK_CANDIDATE "${CMAKE_CURRENT_SOURCE_DIR}/assets/tools/AmodPack.exe")
endif()
if(AMODPACK_CANDIDATE)
    add_custom_command(TARGET AFModConstructor POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:AFModConstructor>/tools
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${AMODPACK_CANDIDATE}" $<TARGET_FILE_DIR:AFModConstructor>/tools/AmodPack.exe
        COMMENT "Copying AmodPack.exe"
    )
endif()

# Pack subsystem packages into ~/.bazkamaz/packages/*.zip
if(DEFINED ENV{USERPROFILE})
    set(PACKAGE_HOME "$ENV{USERPROFILE}/.bazkamaz/packages")
else()
    set(PACKAGE_HOME "$ENV{HOME}/.bazkamaz/packages")
endif()
file(GLOB PACKAGE_DIRS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}/packages ${CMAKE_CURRENT_SOURCE_DIR}/packages/*)
add_custom_command(TARGET AFModConstructor POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${PACKAGE_HOME}
)
foreach(PACKAGE_DIR ${PACKAGE_DIRS})
    if(IS_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/packages/${PACKAGE_DIR})
        add_custom_command(TARGET AFModConstructor POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E chdir ${CMAKE_CURRENT_SOURCE_DIR}/packages/${PACKAGE_DIR}
                    ${CMAKE_COMMAND} -E tar cf
                    ${PACKAGE_HOME}/${PACKAGE_DIR}.zip --format=zip .
            COMMENT "Packing ${PACKAGE_DIR}.zip"
            VERBATIM
        )
    endif()
endforeach()
