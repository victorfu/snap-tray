# ============================================================================
# SnapTray Tests Configuration
# ============================================================================

# Enable automoc for tests
set(CMAKE_AUTOMOC ON)

# Common source files needed for testing ColorAndWidthWidget
set(COLORANDWIDTHWIDGET_COMMON_SOURCES
    ${CMAKE_SOURCE_DIR}/src/ColorAndWidthWidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ToolbarStyle.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/sections/ColorSection.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/sections/WidthSection.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/sections/TextSection.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/sections/ArrowStyleSection.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/sections/ShapeSection.cpp
)

# Common headers that need MOC processing
set(COLORANDWIDTHWIDGET_COMMON_HEADERS
    ${CMAKE_SOURCE_DIR}/include/ColorAndWidthWidget.h
    ${CMAKE_SOURCE_DIR}/include/ui/sections/ColorSection.h
    ${CMAKE_SOURCE_DIR}/include/ui/sections/WidthSection.h
    ${CMAKE_SOURCE_DIR}/include/ui/sections/TextSection.h
    ${CMAKE_SOURCE_DIR}/include/ui/sections/ArrowStyleSection.h
    ${CMAKE_SOURCE_DIR}/include/ui/sections/ShapeSection.h
)

# Helper function to create a test executable
function(add_colorandwidthwidget_test TEST_NAME TEST_SOURCE)
    add_executable(${TEST_NAME}
        ${TEST_SOURCE}
        ${COLORANDWIDTHWIDGET_COMMON_SOURCES}
        ${COLORANDWIDTHWIDGET_COMMON_HEADERS}
    )

    target_include_directories(${TEST_NAME} PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_BINARY_DIR}/generated
    )

    target_link_libraries(${TEST_NAME} PRIVATE
        Qt6::Widgets
        Qt6::Gui
        Qt6::Svg
        Qt6::Test
    )

    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})

    set_tests_properties(${TEST_NAME} PROPERTIES
        TIMEOUT 60
    )
endfunction()

# Create individual test executables
add_colorandwidthwidget_test(ColorAndWidthWidget_State
    ColorAndWidthWidget/tst_State.cpp)

add_colorandwidthwidget_test(ColorAndWidthWidget_Signals
    ColorAndWidthWidget/tst_Signals.cpp)

add_colorandwidthwidget_test(ColorAndWidthWidget_HitTest
    ColorAndWidthWidget/tst_HitTest.cpp)

add_colorandwidthwidget_test(ColorAndWidthWidget_Events
    ColorAndWidthWidget/tst_Events.cpp)
