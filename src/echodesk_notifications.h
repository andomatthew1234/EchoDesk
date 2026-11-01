#ifndef ECHODESK_NOTIFICATIONS_H
#define ECHODESK_NOTIFICATIONS_H

#include <stddef.h>
#include <stdint.h>

constexpr uint8_t ECHODESK_NOTIFICATION_PROTOCOL_VERSION = 1;
constexpr size_t ECHODESK_NOTIFICATION_CAPACITY = 8;
constexpr size_t ECHODESK_NOTIFICATION_SOURCE_MAX_BYTES = 32;
constexpr size_t ECHODESK_NOTIFICATION_TITLE_MAX_BYTES = 64;
constexpr size_t ECHODESK_NOTIFICATION_BODY_MAX_BYTES = 160;

enum class EchoDeskNotificationResult : uint8_t {
    OK = 0,
    NOT_ENABLED,
    INVALID_ID,
    INVALID_SOURCE,
    INVALID_TITLE,
    INVALID_BODY,
    NOT_FOUND,
    OUT_OF_MEMORY,
};

struct EchoDeskNotificationInput {
    uint32_t id = 0;
    uint64_t timestamp_epoch_seconds = 0;
    const char *source = nullptr;
    const char *title = nullptr;
    const char *body = nullptr;
};

struct EchoDeskNotificationItem {
    uint32_t id;
    uint64_t timestamp_epoch_seconds;
    char source[ECHODESK_NOTIFICATION_SOURCE_MAX_BYTES + 1];
    char title[ECHODESK_NOTIFICATION_TITLE_MAX_BYTES + 1];
    char body[ECHODESK_NOTIFICATION_BODY_MAX_BYTES + 1];
};

struct EchoDeskNotificationSnapshot {
    uint32_t revision;
    bool enabled;
    uint8_t count;
    EchoDeskNotificationItem items[ECHODESK_NOTIFICATION_CAPACITY];
};

// Notification content is intentionally runtime-only. init() clears the queue;
// neither this module nor its callers write titles or bodies to flash.
void echodesk_notifications_init();
void echodesk_notifications_set_enabled(bool enabled);
EchoDeskNotificationResult echodesk_notifications_push(
    const EchoDeskNotificationInput &notification);
EchoDeskNotificationResult echodesk_notifications_dismiss(uint32_t id);
void echodesk_notifications_clear();
void echodesk_notifications_capture(EchoDeskNotificationSnapshot &out);

// Validates a decoded JSON string as bounded, printable UTF-8. Length limits
// are byte limits so transport and fixed-size storage have identical bounds.
bool echodesk_notification_text_valid(const char *value, size_t max_bytes,
                                      bool allow_empty);
const char *echodesk_notification_result_message(EchoDeskNotificationResult result);

#endif // ECHODESK_NOTIFICATIONS_H
