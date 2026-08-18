add_executable(dripd
    main.cpp
    localapi.cpp
    tailnet.cpp
    transfers.cpp
    inbox.cpp
    notifier.cpp
    dbusservice.cpp
    settings.cpp
)

target_link_libraries(dripd PRIVATE
    driputil
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
    Qt6::DBus
    KF6::ConfigCore
    KF6::CoreAddons
    KF6::I18n
    KF6::Notifications
    KF6::KIOGui
)

install(TARGETS dripd ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})
install(FILES drip.notifyrc DESTINATION ${KDE_INSTALL_KNOTIFYRCDIR})
