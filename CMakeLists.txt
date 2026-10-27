cmake_minimum_required(VERSION 3.21)

project(OpenSkiProConnect VERSION 0.6.4 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

option(OSC_BUILD_TESTS "Build unit tests" ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets Network WebSockets HttpServer)

qt_standard_project_setup()

# core: the only code that talks to the API of OpenSkiPro2 (docs/design.md 2)
add_library(osc_core STATIC
    src/core/Log.h            src/core/Log.cpp
    src/core/Profile.h        src/core/Profile.cpp
    src/core/ApiClient.h      src/core/ApiClient.cpp
    src/core/RaceModel.h      src/core/RaceModel.cpp
    src/core/Module.h
)
target_include_directories(osc_core PUBLIC src)
target_link_libraries(osc_core PUBLIC Qt6::Core Qt6::Network Qt6::WebSockets)
target_compile_definitions(osc_core PUBLIC OSC_VERSION="${PROJECT_VERSION}")

# look of OpenSkiPro2 (style sheet, icons, navigation panel)
add_library(osc_ui STATIC
    src/ui/UiHelpers.h        src/ui/UiHelpers.cpp
    src/ui/NavPanel.h         src/ui/NavPanel.cpp
)
target_include_directories(osc_ui PUBLIC src)
target_link_libraries(osc_ui PUBLIC Qt6::Widgets)

# module Scoreboard: results boards (port of SkiAlpBridgeDisplay)
add_library(osc_scoreboard STATIC
    src/modules/scoreboard/boardfieldrule.h          src/modules/scoreboard/boardfieldrule.cpp
    src/modules/scoreboard/boardprogramengine.h      src/modules/scoreboard/boardprogramengine.cpp
    src/modules/scoreboard/boardcontentcontroller.h  src/modules/scoreboard/boardcontentcontroller.cpp
    src/modules/scoreboard/displaydriver.h
    src/modules/scoreboard/dispvolaboardprodriver.h  src/modules/scoreboard/dispvolaboardprodriver.cpp
    src/modules/scoreboard/dispthdis08driver.h       src/modules/scoreboard/dispthdis08driver.cpp
    src/modules/scoreboard/displayconfig.h           src/modules/scoreboard/displayconfig.cpp
    src/modules/scoreboard/ScoreboardModule.h        src/modules/scoreboard/ScoreboardModule.cpp
)
target_include_directories(osc_scoreboard PUBLIC src/modules/scoreboard)
target_link_libraries(osc_scoreboard PUBLIC osc_core osc_ui Qt6::Widgets)

# module FIS Live: the race to the FIS Live server (FIS Live XML v53)
add_library(osc_fislive STATIC
    src/modules/fislive/FisLiveXml.h      src/modules/fislive/FisLiveXml.cpp
    src/modules/fislive/FisLiveSender.h   src/modules/fislive/FisLiveSender.cpp
    src/modules/fislive/FisLiveModule.h   src/modules/fislive/FisLiveModule.cpp
)
target_include_directories(osc_fislive PUBLIC src/modules/fislive)
target_link_libraries(osc_fislive PUBLIC osc_core osc_ui Qt6::Widgets Qt6::Network)

# module Screen: web server with the boards and the speaker page (docs/screen-design.md)
add_library(osc_screen STATIC
    src/modules/screen/ScreenConfig.h     src/modules/screen/ScreenConfig.cpp
    src/modules/screen/ScreenData.h       src/modules/screen/ScreenData.cpp
    src/modules/screen/ScreenServer.h     src/modules/screen/ScreenServer.cpp
    src/modules/screen/ScreenEditor.h     src/modules/screen/ScreenEditor.cpp
    src/modules/screen/ScreenModule.h     src/modules/screen/ScreenModule.cpp
)
qt_add_resources(osc_screen "screen_web" PREFIX "/screen/web" BASE src/modules/screen/web FILES
    src/modules/screen/web/board.html     src/modules/screen/web/board.js
    src/modules/screen/web/speaker.html   src/modules/screen/web/speaker.js
    src/modules/screen/web/common.js      src/modules/screen/web/screen.css
)
target_include_directories(osc_screen PUBLIC src/modules/screen)
target_link_libraries(osc_screen PUBLIC osc_core osc_ui Qt6::Widgets Qt6::Network Qt6::WebSockets Qt6::HttpServer)

# module Publish: the race on the internet through OpenSkiPro Publish (docs/publish-design.md)
add_library(osc_publish STATIC
    src/modules/publish/PublishSections.h src/modules/publish/PublishSections.cpp
    src/modules/publish/PublishClient.h   src/modules/publish/PublishClient.cpp
    src/modules/publish/PublishModule.h   src/modules/publish/PublishModule.cpp
)
target_include_directories(osc_publish PUBLIC src/modules/publish)
target_link_libraries(osc_publish PUBLIC osc_core osc_ui osc_screen Qt6::Widgets Qt6::Network)

qt_add_executable(OpenSkiProConnect WIN32
    src/app/main.cpp
    src/app/MainWindow.h      src/app/MainWindow.cpp
    resources/resources.qrc
    resources/app.rc
)
target_link_libraries(OpenSkiProConnect PRIVATE osc_core osc_ui osc_scoreboard osc_fislive osc_screen osc_publish Qt6::Widgets)

# apiserver-replay: serves a recording of osp-apicheck as a fake OpenSkiPro2 (development, tests)
add_library(osc_replay STATIC tools/apiserver-replay/ReplayServer.h tools/apiserver-replay/ReplayServer.cpp)
target_include_directories(osc_replay PUBLIC tools/apiserver-replay)
target_link_libraries(osc_replay PUBLIC Qt6::Core Qt6::Network Qt6::WebSockets Qt6::HttpServer)
add_executable(apiserver-replay tools/apiserver-replay/main.cpp)
target_link_libraries(apiserver-replay PRIVATE osc_replay)

# the tests are not part of the public repository
if(OSC_BUILD_TESTS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt")
    enable_testing()
    add_subdirectory(tests)
endif()
