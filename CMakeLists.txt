cmake_minimum_required(VERSION 3.24)

project(drip VERSION 0.1.0 LANGUAGES CXX)

set(QT_MIN_VERSION 6.6.0)
set(KF_MIN_VERSION 6.0.0)

find_package(ECM ${KF_MIN_VERSION} REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH})

include(KDEInstallDirs)
include(KDECMakeSettings)
include(KDECompilerSettings NO_POLICY_SCOPE)
include(FeatureSummary)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 ${QT_MIN_VERSION} REQUIRED COMPONENTS
    Core
    Gui
    Widgets
    Network
    DBus
    Qml
    Quick
)

find_package(KF6 ${KF_MIN_VERSION} REQUIRED COMPONENTS
    Config
    CoreAddons
    I18n
    Notifications
    KIO
)

add_subdirectory(common)
add_subdirectory(daemon)
add_subdirectory(plugin)

# The applet is pure QML/JSON, installed as a KPackage.
install(DIRECTORY applet/
    DESTINATION ${KDE_INSTALL_DATADIR}/plasma/plasmoids/dev.drip.applet
)

feature_summary(WHAT ALL FATAL_ON_MISSING_REQUIRED_PACKAGES)
