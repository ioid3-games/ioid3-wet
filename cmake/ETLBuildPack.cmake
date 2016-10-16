#-----------------------------------------------------------------
# Build Pack
#-----------------------------------------------------------------

#
# pak3.pk3
#
if(ZIP_EXECUTABLE)
	add_custom_target(
		pak3_pk3 ALL
		COMMAND ${ZIP_EXECUTABLE} -q -r ${CMAKE_CURRENT_BINARY_DIR}/legacy/pak3_${ETL_CMAKE_VERSION_SHORT}.pk3 *
		WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/etmain/
	)

	install(FILES ${CMAKE_CURRENT_BINARY_DIR}/legacy/pak3_${ETL_CMAKE_VERSION_SHORT}.pk3
		DESTINATION "${INSTALL_DEFAULT_MODDIR}/legacy"
	)
endif()
