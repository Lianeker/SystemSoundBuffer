cmake_minimum_required(VERSION 3.20)
project(SystemSoundBuffer C CXX)

# ---------------------------------------------------------------- NAppGUI
#
# find_package() va ANTES de crear ningun target, y no es un detalle de estilo:
# el paquete de NAppGUI fija CMAKE_MSVC_RUNTIME_LIBRARY para que el consumidor
# use el mismo CRT que el SDK. Llamandolo despues, los targets ya creados se
# quedan con el CRT por omision y el enlace falla con __imp_fwrite sin resolver.
#
# El paquete instalado hoy solo exporta la configuracion Debug, asi que la
# interfaz hay que compilarla en Debug. Para Release, instalar antes el SDK en
# Release desde el proyecto de al lado.
set(SSB_NAPPGUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../nappgui/install/cmake"
    CACHE PATH "Directorio con nappgui-config.cmake")

find_package(nappgui QUIET PATHS "${SSB_NAPPGUI_DIR}" NO_DEFAULT_PATH)

# La linea base es cero avisos, como en el proyecto de al lado. Si algo avisa,
# se arregla; no se baja el nivel.
if(MSVC)
    add_compile_options(/W4 /WX)
    # fopen/snprintf no son "inseguras": son C estandar. Las alternativas _s no
    # existen fuera de MSVC y este motor tiene que compilar tambien en Linux.
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
else()
    add_compile_options(-Wall -Wextra -Werror)
endif()

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(SSB_SOURCES
    ssb_core/src/ssb_codec.c
    ssb_core/src/ssb_mix.c
    ssb_core/src/ssb_ring.c
    ssb_core/src/ssb_track.c
    ssb_core/src/ssb_wav.c
    ssb_core/src/ssb_util.c
    ssb_core/src/ssb_watch.c)

if(WIN32)
    list(APPEND SSB_SOURCES ssb_core/src/win/ssb_capture_win.cpp
                            ssb_core/src/win/ssb_encode_win.cpp
                            ssb_core/src/win/ssb_play_win.cpp)
elseif(UNIX AND NOT APPLE)
    list(APPEND SSB_SOURCES ssb_core/src/linux/ssb_capture_linux.c
                            ssb_core/src/linux/ssb_encode_linux.c
                            ssb_core/src/linux/ssb_play_linux.c)
else()
    message(FATAL_ERROR "Plataforma no soportada. El alcance es Windows y Linux (docs/01, seccion 6).")
endif()

add_library(ssb_core STATIC ${SSB_SOURCES})
target_include_directories(ssb_core PUBLIC ssb_core/include)
target_include_directories(ssb_core PRIVATE ssb_core/src)

if(WIN32)
    target_link_libraries(ssb_core PRIVATE ole32 mmdevapi propsys avrt
                                       mf mfplat mfreadwrite mfuuid)
else()
    find_package(Threads REQUIRED)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(PULSE REQUIRED libpulse libpulse-simple)
    target_include_directories(ssb_core PRIVATE ${PULSE_INCLUDE_DIRS})
    # CMAKE_DL_LIBS es -ldl donde haga falta y vacio donde dlopen ya esta en
    # libc (glibc 2.34 en adelante). El codificador de MP3 carga LAME asi.
    target_link_libraries(ssb_core PRIVATE Threads::Threads m ${PULSE_LIBRARIES} ${CMAKE_DL_LIBS})
    target_link_directories(ssb_core PRIVATE ${PULSE_LIBRARY_DIRS})
endif()

add_executable(ssb ssb_core/tools/ssb_cli.c)
target_link_libraries(ssb PRIVATE ssb_core)

# --------------------------------------------------------------- interfaz

# El SDK instalado puede no exportar todas las configuraciones. Enlazar Release
# contra los .lib de Debug falla con simbolos del CRT de depuracion sin resolver,
# asi que mejor omitir la interfaz con un mensaje claro que romper el build.
set(SSB_GUI_OK ${nappgui_FOUND})
if(nappgui_FOUND AND MSVC)
    if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug"
       AND NOT EXISTS "${SSB_NAPPGUI_DIR}/nappgui-targets-release.cmake")
        set(SSB_GUI_OK FALSE)
        message(STATUS "Interfaz: OMITIDA, el SDK solo esta instalado en Debug")
        message(STATUS "          usa -DCMAKE_BUILD_TYPE=Debug, o instala el SDK en Release")
    endif()
endif()

# En un CI, omitir la interfaz en silencio es lo peor que puede pasar: el job
# compila solo el motor, pasa en verde y nadie se entera de que la mitad del
# proyecto no se ha tocado. Con esto puesto, omitirla es un fallo.
option(SSB_REQUIRE_GUI "Fallar si la interfaz no se puede construir" OFF)
if(SSB_REQUIRE_GUI AND NOT SSB_GUI_OK)
    message(FATAL_ERROR "Interfaz: se pidio SSB_REQUIRE_GUI y no se puede construir")
endif()

if(SSB_GUI_OK)
    add_executable(ssbgui WIN32 ssb_gui/ssbapp.c ssb_gui/ssbwave.c ssb_gui/ssbcmd.c
                             ssb_gui/ssbicon.c ssb_gui/ssbtext.c)
    target_include_directories(ssbgui PRIVATE ssb_gui)
    target_link_libraries(ssbgui PRIVATE ssb_core nappgui::osapp)
    message(STATUS "Interfaz: se construye")
elseif(NOT nappgui_FOUND)
    message(STATUS "Interfaz: OMITIDA, no encuentro nappgui en ${SSB_NAPPGUI_DIR}")
    message(STATUS "          instalalo desde el proyecto de al lado: tools/verify.ps1")
endif()

enable_testing()
add_test(NAME selftest COMMAND ssb selftest)
