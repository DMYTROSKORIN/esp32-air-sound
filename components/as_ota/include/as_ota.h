#pragma once
// A/B firmware update with a signed image trailer. Same format and tooling as
// esp32-s3-n16r8-bastion (tools/ota_sign.py):
//
//   [ ESP-IDF app image ][ 32-byte version, NUL-padded ][ 64-byte Ed25519 signature ]
//
// The signature covers SHA-256(image || version) and is verified against the
// release public key compiled in (as_ota_public_key.h). The stream is written
// straight into the inactive slot while the hash is computed; nothing is
// committed until the signature and esp_ota_end() both pass. After the reboot
// the bootloader marks the slot PENDING_VERIFY; as_ota_self_test_start()
// confirms it once the application reports it is serving (Wi-Fi up, transmitter
// on air) or lets the bootloader roll back.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AS_OTA_VERSION_FIELD 32
#define AS_OTA_SIGNATURE 64

typedef struct {
    bool ok;
    char message[160];
    char version[AS_OTA_VERSION_FIELD + 1];
    const char *target_label;
    size_t image_bytes;
} as_ota_result_t;

typedef void (*as_ota_report_fn)(const char *line, void *user);

// Downloads `url` (http:// or https://, public CA bundle) and applies it.
// Returns result.ok; the caller reboots after reporting. The image must be for this board (project
// name) and, unless as_ota_allow_downgrade(true) was called for it, not older than the running
// firmware; both are checked on the image's first bytes, before anything is written.
bool as_ota_from_url(const char *url, as_ota_report_fn report, void *user, as_ota_result_t *result);
// Lets the next as_ota_from_url install an older version (the command's `downgrade`). Cleared by that call.
void as_ota_allow_downgrade(bool on);

// Human-readable slot summary for the System screen / status message.
void as_ota_describe(char *out, size_t cap);
// Switches the boot partition to the other slot if it holds a valid image.
bool as_ota_rollback(char *message, size_t cap);

// Self-test after an update.
void as_ota_self_test_start(void);
bool as_ota_self_test_pending(void);
// The application calls this once it is fully serving its purpose.
void as_ota_note_service_up(void);

// Release check against GitHub Releases of `repo` ("owner/name"). Assets are
// looked up by `asset_name` ("esp32-air-sound-signed.bin"); only release tags
// starting with `tag_prefix` ("v") are considered. The repository is public, so
// the boards read GitHub directly over TLS with the public CA bundle.
typedef struct {
    bool checked;
    bool newer;
    char latest_version[32];
    char url[256];
    char error[160];
    uint32_t checked_at_ms;
} as_ota_update_info_t;

void as_ota_configure_release_check(const char *repo, const char *tag_prefix, const char *asset_name);
bool as_ota_check_for_update(as_ota_update_info_t *out);
void as_ota_get_update_info(as_ota_update_info_t *out);
// Daily checker task; installs automatically when the config allows and
// `may_install()` returns true (e.g. night, battery above 40 %, no voice session).
typedef bool (*as_ota_may_install_fn)(void);
void as_ota_checker_start(as_ota_may_install_fn may_install);
// Running firmware version (from the app descriptor).
const char *as_ota_running_version(void);
// True while an update is being written.
bool as_ota_in_progress(void);

#ifdef __cplusplus
}
#endif
