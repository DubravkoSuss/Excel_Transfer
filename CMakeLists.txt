cmake_minimum_required(VERSION 3.16)
project(Excel_transfer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Concurrent AxContainer)
find_package(Qt6 REQUIRED COMPONENTS CorePrivate)

add_executable(ExcelTransfer WIN32
        ui/main.cpp
        ui/mainwindow.cpp
        ui/mainwindow.h
        ui/individualtransfertab.cpp
        ui/individualtransfertab.h
        ui/fillallmonthstab.cpp
        ui/fillallmonthstab.h
        ui/comparatortab.cpp
        ui/comparatortab.h
        ui/hybridtransfertab.cpp
        ui/hybridtransfertab.h
        ui/customtabbar.cpp
        ui/customtabbar.h
        ui/excelsearchtab.cpp
        ui/excelsearchtab.h
        ui/yearrollovertab.cpp
        ui/yearrollovertab.h
        features/search/excelsearchservice.cpp
        features/search/excelsearchservice.h
        features/search/searchworker.cpp
        features/search/searchworker.h
        features/rollover/rolloverworker.cpp
        features/rollover/rolloverworker.h
        services/excelhandler.cpp
        services/excelhandler.h
        services/breaklinks.cpp
        services/breaklinks.h
        core/mappingsmanager.cpp
        core/mappingsmanager_sap_ytd.cpp
        core/mappingsmanager.h
        services/transferservice.cpp
        services/transferservice.h
        features/transfer/rollingtransferservice.cpp
        features/transfer/rollingtransferservice.h
        services/loadservice.cpp
        services/loadservice.h
        services/mappingservice.cpp
        services/mappingservice.h
        core/mappingcontroller.cpp
        core/mappingcontroller.h
        core/mappingmodel.cpp
        core/mappingmodel.h
        core/periodcontroller.cpp
        core/periodcontroller.h
        core/periodmodel.cpp
        core/periodmodel.h
        features/transfer/fillallmodels.h
        features/transfer/hybridtransferconfig.h
        features/transfer/hybridworker.cpp
        features/transfer/hybridworker.h
        features/periods/periodrow.cpp
        features/periods/periodrow.h
        features/periods/yearcard.cpp
        features/periods/yearcard.h
        features/periods/quarterquickselector.cpp
        features/periods/quarterquickselector.h
        features/mappings/mappingrow.cpp
        features/mappings/mappingrow.h
        features/mappings/mappingpopup.cpp
        features/mappings/mappingpopup.h
        features/mappings/rowmapvalidatordialog.cpp
        features/mappings/rowmapvalidatordialog.h
        features/transfer/transferworker.h
        features/transfer/mappingupdater.h
        features/transfer/mappingupdater.cpp
        features/transfer/loadworker.cpp
        features/transfer/loadworker.h
        features/transfer/rollingworker.h
        features/transfer/fillallworker.h
        features/transfer/fillallworker.cpp
        features/transfer/individualtransferpanel.cpp
        features/transfer/individualtransferpanel.h
        ui/sheetcellselectordialog.cpp
        ui/sheetcellselectordialog.h
        ui/ignorerowsdialog.cpp
        ui/ignorerowsdialog.h
        ui/toastwidget.cpp
        ui/toastwidget.h
        resources.qrc
)

target_link_libraries(ExcelTransfer PRIVATE
        Qt6::Core
        Qt6::CorePrivate
        Qt6::Gui
        Qt6::Widgets
        Qt6::Concurrent
        Qt6::AxContainer
)

# Link Windows DWM API for title bar customization
if(WIN32)
    target_link_libraries(ExcelTransfer PRIVATE dwmapi)
    
    # Set application icon (if you have app_icon.rc)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/app_icon.rc")
        target_sources(ExcelTransfer PRIVATE app_icon.rc)
    endif()
    
    # Hide console window for release builds
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        set_target_properties(ExcelTransfer PROPERTIES
            WIN32_EXECUTABLE TRUE
            LINK_FLAGS "/SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup"
        )
    endif()
endif()