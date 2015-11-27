#set(UI_FILES
#
#    ${CMAKE_CURRENT_SOURCE_DIR}/qt/main_ui.cpp
#    ${CMAKE_CURRENT_SOURCE_DIR}/qt/main_ui.h
#    ${CMAKE_CURRENT_SOURCE_DIR}/qt/mainwindow.cpp
#    ${CMAKE_CURRENT_SOURCE_DIR}/qt/droparea.cpp
#    ${CMAKE_CURRENT_SOURCE_DIR}/qt/dropsitewindow.cpp
#    ${CMAKE_CURRENT_SOURCE_DIR}/qt/osdepend.cpp
#    ${CMAKE_CURRENT_SOURCE_DIR}/qt/VGMFileView.cpp
#    ${CMAKE_CURRENT_SOURCE_DIR}/qt/VGMFileView.h
#    ${CMAKE_CURRENT_SOURCE_DIR}/qt/HexView.cpp
#    ${CMAKE_CURRENT_SOURCE_DIR}/qt/HexView.h
#    ${CMAKE_CURRENT_SOURCE_DIR}/qt/Helpers.h
#    ${CMAKE_CURRENT_SOURCE_DIR}/qt/Helpers.cpp
#    ${CMAKE_CURRENT_SOURCE_DIR}/qt/MdiArea.cpp
#    ${CMAKE_CURRENT_SOURCE_DIR}/qt/MdiArea.h
#    ${CMAKE_CURRENT_SOURCE_DIR}/qt/VGMFileView.cpp
#    ${CMAKE_CURRENT_SOURCE_DIR}/qt/Helpers.h
#
#    ${CMAKE_CURRENT_SOURCE_DIR}/qt/QtVGMRoot.cpp
#    ${CMAKE_CURRENT_SOURCE_DIR}/qt/VGMFileListView.cpp
#    ${CMAKE_CURRENT_SOURCE_DIR}/qt/RawFileListView.cpp
#    ${CMAKE_CURRENT_SOURCE_DIR}/qt/VGMCollListView.cpp
#    ${CMAKE_CURRENT_SOURCE_DIR}/qt/MusicPlayer.cpp
#
#    ${CMAKE_CURRENT_SOURCE_DIR}/qt/mainwindow.h
#    ${CMAKE_CURRENT_SOURCE_DIR}/qt/droparea.h
#    ${CMAKE_CURRENT_SOURCE_DIR}/qt/dropsitewindow.h
#    ${CMAKE_CURRENT_SOURCE_DIR}/qt/osdepend.h
#    ${CMAKE_CURRENT_SOURCE_DIR}/qt/QtVGMRoot.h
#    ${CMAKE_CURRENT_SOURCE_DIR}/qt/VGMFileListView.h
#    ${CMAKE_CURRENT_SOURCE_DIR}/qt/RawFileListView.h
#    ${CMAKE_CURRENT_SOURCE_DIR}/qt/VGMCollListView.h
#    ${CMAKE_CURRENT_SOURCE_DIR}/qt/MusicPlayer.h
#
#    PARENT_SCOPE
#)

#source_group("ui" FILES ${UI_FILES})

#set(SOURCE_FILES
#    ${SOURCE_FILES}
#    ${UI_FILES}
#    PARENT_SCOPE
#)

set(INCLUDE_DIRS
    ${INCLUDE_DIRS}

    ${CMAKE_CURRENT_SOURCE_DIR}/qt

    PARENT_SCOPE
)

#add_library(ui ${UI_FILES})


SET(RESOURCE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/qt/resources/resources.qrc" PARENT_SCOPE)
