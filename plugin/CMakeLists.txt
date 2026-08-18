# The QML plugin the applet imports. Talks to dripd over D-Bus and holds no
# state of its own beyond the models it mirrors.
qt_add_qml_module(dripplugin
    URI dev.drip
    VERSION 1.0
    PLUGIN_TARGET dripplugin
    CLASS_NAME DripPlugin
    NO_GENERATE_PLUGIN_SOURCE
    SOURCES
        dripplugin.cpp
        dripclient.cpp
        dripclient.h
        jsonlistmodel.cpp
        jsonlistmodel.h
        avatarprovider.cpp
        avatarprovider.h
)

# ECM strips the "lib" prefix from plugins, but Qt's QML loader resolves
# `plugin dripplugin` in qmldir to libdripplugin.so. Put the prefix back.
set_target_properties(dripplugin PROPERTIES PREFIX "lib")

target_link_libraries(dripplugin PRIVATE
    driputil
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::DBus
    Qt6::Network
    KF6::KIOGui
)

install(TARGETS dripplugin DESTINATION ${KDE_INSTALL_QMLDIR}/dev/drip)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/dripplugin.qmltypes
    ${CMAKE_CURRENT_BINARY_DIR}/qmldir
    DESTINATION ${KDE_INSTALL_QMLDIR}/dev/drip
)
