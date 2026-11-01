set(SENSORNODE_SYSTEMD_UNITDIR "lib/systemd/system" CACHE PATH "systemd system unit directory")
set(SENSORNODE_TMPFILESDIR     "lib/tmpfiles.d"     CACHE PATH "tmpfiles.d directory")
set(SENSORNODE_UDEVRULESDIR    "lib/udev/rules.d"   CACHE PATH "udev rules directory")

configure_file(sensornode-telemetryd.service.in sensornode-telemetryd.service @ONLY)
configure_file(sensornode-otad.service.in sensornode-otad.service @ONLY)
configure_file(sensornode-settingsd.service.in sensornode-settingsd.service @ONLY)
configure_file(sensornode-displayd.service.in sensornode-displayd.service @ONLY)

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/sensornode-telemetryd.service
              ${CMAKE_CURRENT_BINARY_DIR}/sensornode-otad.service
              ${CMAKE_CURRENT_BINARY_DIR}/sensornode-settingsd.service
              ${CMAKE_CURRENT_BINARY_DIR}/sensornode-displayd.service
        DESTINATION ${SENSORNODE_SYSTEMD_UNITDIR})
install(FILES sensornode-tmpfiles.conf
        DESTINATION ${SENSORNODE_TMPFILESDIR} RENAME sensornode.conf)
install(FILES 60-sensornode-i2c.rules
        DESTINATION ${SENSORNODE_UDEVRULESDIR})
install(FILES sensornode-telemetryd.default
        DESTINATION ${CMAKE_INSTALL_FULL_SYSCONFDIR}/default RENAME sensornode-telemetryd)
install(FILES sensornode-otad.default
        DESTINATION ${CMAKE_INSTALL_FULL_SYSCONFDIR}/default RENAME sensornode-otad)
install(FILES sensornode-settingsd.default
        DESTINATION ${CMAKE_INSTALL_FULL_SYSCONFDIR}/default RENAME sensornode-settingsd)
install(FILES sensornode-displayd.default
        DESTINATION ${CMAKE_INSTALL_FULL_SYSCONFDIR}/default RENAME sensornode-displayd)
