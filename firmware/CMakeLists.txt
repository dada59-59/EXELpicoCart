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
    pico_generate_pio_header(${nom} ${CMAKE_CURRENT_LIST_DIR}/exlir.pio)
    target_link_libraries(${nom} pico_stdlib pico_multicore hardware_clocks hardware_interp hardware_pio hardware_flash)
    pico_add_extra_outputs(${nom})
endfunction()

# souris USB (port USB du Pico en hote, TinyUSB) : seulement les firmwares cartouche ;
# et, Pico seul sur un PC, port serie (peripherique) pour relire les sauvegardes
function(exl_souris nom)
    target_sources(${nom} PRIVATE exlmouse.c exlusbdev.c)
    target_include_directories(${nom} PRIVATE ${CMAKE_CURRENT_LIST_DIR})
    target_compile_definitions(${nom} PRIVATE EXL_WITH_MOUSE=1)
    target_link_libraries(${nom} tinyusb_host tinyusb_device)
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
exl_souris(exlcart)
exl_sonde(exlsonde)

exl_cartouche(exlcart_eprom)
exl_souris(exlcart_eprom)
target_compile_definitions(exlcart_eprom PRIVATE EXL_PINOUT_EPROM=1)
exl_sonde(exlsonde_eprom)
target_compile_definitions(exlsonde_eprom PRIVATE EXL_PINOUT_EPROM=1)

# sonde "A1" : declenchee par le doigt A1 (NROMCS) sur GP26, A8 (SPARIN7) sur GP27
exl_sonde(exlsonde_a1)
target_compile_definitions(exlsonde_a1 PRIVATE EXL_SONDE_A1=1)
exl_sonde(exlsonde_a1_eprom)
target_compile_definitions(exlsonde_a1_eprom PRIVATE EXL_PINOUT_EPROM=1 EXL_SONDE_A1=1)

# EXLRAM : essai de cartouche RAM (fil volant A1 -> GP26)
function(exl_ram nom)
    add_executable(${nom} exlram.c)
    pico_generate_pio_header(${nom} ${CMAKE_CURRENT_LIST_DIR}/exlram.pio)
    target_link_libraries(${nom} pico_stdlib pico_multicore hardware_clocks hardware_pio hardware_interp)
    pico_enable_stdio_usb(${nom} 0)
    pico_enable_stdio_uart(${nom} 0)
    pico_add_extra_outputs(${nom})
endfunction()
exl_ram(exlram)
exl_ram(exlram_eprom)
target_compile_definitions(exlram_eprom PRIVATE EXL_PINOUT_EPROM=1)

# EXLSCOPE : le Pico en oscilloscope (ADC sur GP28) pour le crayon optique
function(exl_scope nom)
    add_executable(${nom} exlscope.c)
    target_link_libraries(${nom} pico_stdlib pico_multicore hardware_clocks hardware_flash hardware_adc hardware_dma hardware_interp)
    pico_enable_stdio_usb(${nom} 1)
    pico_enable_stdio_uart(${nom} 0)
    pico_add_extra_outputs(${nom})
endfunction()
exl_scope(exlscope)
exl_scope(exlscope_eprom)
target_compile_definitions(exlscope_eprom PRIVATE EXL_PINOUT_EPROM=1)

# EXLBROCHES : test des lignes de donnees (D0-D7 a 0 puis a 1, 5 s chacun), sans l'EXL100
foreach(nom exlbroches exlbroches_eprom)
    add_executable(${nom} exlbroches.c)
    target_link_libraries(${nom} pico_stdlib hardware_interp)
    pico_add_extra_outputs(${nom})
endforeach()
target_compile_definitions(exlbroches_eprom PRIVATE EXL_PINOUT_EPROM=1)


# variantes d'essai pour l'emulateur rp2040js (un seul coeur) : non livrees
exl_cartouche(exlcart_test)
target_compile_definitions(exlcart_test PRIVATE EXL_SINGLE_CORE_TEST=1 EXL_VIDEO_TEST=1)
exl_cartouche(exlcart_eprom_test)
target_compile_definitions(exlcart_eprom_test PRIVATE EXL_SINGLE_CORE_TEST=1 EXL_VIDEO_TEST=1 EXL_PINOUT_EPROM=1)
# bibliotheque + peripheriques, sans service du bus (le coeur 0 seul) : non livrees
exl_cartouche(exlcart_libio_test)
target_compile_definitions(exlcart_libio_test PRIVATE EXL_LIBIO_TEST=1)
exl_cartouche(exlcart_eprom_libio_test)
target_compile_definitions(exlcart_eprom_libio_test PRIVATE EXL_LIBIO_TEST=1 EXL_PINOUT_EPROM=1)
# Pico seul sur un PC : port serie USB et sauvegardes de l'Exelmemoire (le coeur 0 seul) : non livrees
exl_cartouche(exlcart_usbdev_test)
exl_souris(exlcart_usbdev_test)
target_compile_definitions(exlcart_usbdev_test PRIVATE EXL_LIBIO_TEST=1 EXL_USBDEV_TEST=1)
exl_cartouche(exlcart_eprom_usbdev_test)
exl_souris(exlcart_eprom_usbdev_test)
target_compile_definitions(exlcart_eprom_usbdev_test PRIVATE EXL_LIBIO_TEST=1 EXL_USBDEV_TEST=1 EXL_PINOUT_EPROM=1)
# l'emetteur infrarouge seul (le coeur 0 ne fait que lui) : non livrees
exl_cartouche(exlcart_ir_test)
target_compile_definitions(exlcart_ir_test PRIVATE EXL_IR_TEST=1)
exl_cartouche(exlcart_eprom_ir_test)
target_compile_definitions(exlcart_eprom_ir_test PRIVATE EXL_IR_TEST=1 EXL_PINOUT_EPROM=1)
# bibliotheque + jeu "disque", servis par le coeur 0 seul : non livrees
exl_cartouche(exlcart_libflux_test)
target_compile_definitions(exlcart_libflux_test PRIVATE EXL_SINGLE_CORE_TEST=1 EXL_LIBFLUX_TEST=1)
exl_cartouche(exlcart_eprom_libflux_test)
target_compile_definitions(exlcart_eprom_libflux_test PRIVATE EXL_SINGLE_CORE_TEST=1 EXL_LIBFLUX_TEST=1 EXL_PINOUT_EPROM=1)
