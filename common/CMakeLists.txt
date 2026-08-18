# Helpers shared by the daemon and the QML plugin, which are separate binaries.
add_library(driputil STATIC util.cpp util.h)

target_include_directories(driputil PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(driputil PUBLIC Qt6::Core)

# Linked into a shared module, so it has to be position-independent.
set_target_properties(driputil PROPERTIES POSITION_INDEPENDENT_CODE ON)
