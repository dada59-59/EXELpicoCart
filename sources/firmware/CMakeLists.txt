cmake_minimum_required(VERSION 3.13)
include(pico_sdk_import.cmake)
project(exlcart C CXX ASM)
set(CMAKE_C_STANDARD 11)
pico_sdk_init()

# Deux cartes (voir exlpins.h) :
#   exlcart / exlsonde              : carte LINEAIRE (A0-A14 = GP0-GP14, D0-D7 = GP15-GP22)
#   exlcart_eprom / exlsonde_eprom  : carte SUPPORT EPROM (Pico soude sur l'empreinte 27C256)

# EXLCART : la cartouche emulee
function(exl_cartouche nom)
    add_executable(${nom} exlcart.c)
    target_link_libraries(${nom} pico_stdlib pico_multicore hardware_clocks hardware_interp)
    pico_add_extra_outputs(${nom})
endfunction()

# EXLSONDE : meme service du bus + releve des acces, lu par USB
function(exl_sonde nom)
    add_executable(${nom} exlsonde.c)
    pico_generate_pio_header(${nom} ${CMAKE_CURRENT_LIST_DIR}/exloscillo.pio)
    target_link_libraries(${nom} pico_stdlib pico_multicore hardware_clocks hardware_flash
                          hardware_pio hardware_dma hardware_interp)
    pico_enable_stdio_usb(${nom} 1)
    pico_enable_stdio_uart(${nom} 0)
    pico_add_extra_outputs(${nom})
endfunction()

exl_cartouche(exlcart)
exl_sonde(exlsonde)

exl_cartouche(exlcart_eprom)
target_compile_definitions(exlcart_eprom PRIVATE EXL_PINOUT_EPROM=1)
exl_sonde(exlsonde_eprom)
target_compile_definitions(exlsonde_eprom PRIVATE EXL_PINOUT_EPROM=1)

# variantes d'essai pour l'emulateur rp2040js (un seul coeur) : non livrees
exl_cartouche(exlcart_test)
target_compile_definitions(exlcart_test PRIVATE EXL_SINGLE_CORE_TEST=1)
exl_cartouche(exlcart_eprom_test)
target_compile_definitions(exlcart_eprom_test PRIVATE EXL_SINGLE_CORE_TEST=1 EXL_PINOUT_EPROM=1)
