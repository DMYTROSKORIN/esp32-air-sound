# AirPlay 1 (RAOP) receiver: RTSP, RTP with resend and timing, AES decryption, ALAC decoding,
# DMAP metadata. raop.c/rtp.c/util.c/dmap_parser.c are philippe44's and James Laird's MIT code
# as shipped in squeezelite-esp32; alac/ is Apple's ALAC decoder (Apache 2.0); the rest is ours.
idf_component_register(SRCS "raop.c" "rtp.c" "util.c" "dmap_parser.c" "log_util.c" "alac_wrapper.cpp"
                            "alac/ALACDecoder.cpp" "alac/ALACBitUtilities.c" "alac/ag_dec.c" "alac/dp_dec.c"
                            "alac/matrix_dec.c" "alac/EndianPortable.c"
                       INCLUDE_DIRS "." "alac"
                       REQUIRES mbedtls lwip pthread esp_netif esp_wifi esp_timer espressif__mdns)
target_compile_definitions(${COMPONENT_LIB} PRIVATE TARGET_RT_LITTLE_ENDIAN=1)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-misleading-indentation -Wno-unused-function
                       -Wno-unused-variable -Wno-unused-but-set-variable -Wno-format -Wno-char-subscripts
                       -Wno-implicit-fallthrough -Wno-sign-compare -Wno-maybe-uninitialized
                       -Wno-stringop-overflow -Wno-array-bounds)
set_source_files_properties(alac/ALACDecoder.cpp alac_wrapper.cpp PROPERTIES COMPILE_FLAGS "-Wno-multichar -fno-exceptions")
