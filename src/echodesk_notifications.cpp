#include "echodesk_notifications.h"

#include <stdlib.h>
#include <string.h>

namespace {

EchoDeskNotificationItem *items = nullptr;
uint8_t item_count = 0;
bool notifications_enabled = false;
uint32_t revision = 1;

void bump_revision() {
    ++revision;
    if (revision == 0) revision = 1;
}

void copy_text(char *destination, size_t capacity, const char *source) {
    const size_t length = strlen(source);
    const size_t copy_length = length < capacity - 1 ? length : capacity - 1;
    memcpy(destination, source, copy_length);
    destination[copy_length] = '\0';
}

bool valid_continuation(const unsigned char *value, size_t remaining,
                        size_t required) {
    if (remaining < required) return false;
    for (size_t i = 1; i < required; ++i) {
        if ((value[i] & 0xC0U) != 0x80U) return false;
    }
    return true;
}

} // namespace

bool echodesk_notification_text_valid(const char *value, size_t max_bytes,
                                      bool allow_empty) {
    if (!value) return false;
    const size_t length = strlen(value);
    if ((!allow_empty && length == 0) || length > max_bytes) return false;

    const auto *bytes = reinterpret_cast<const unsigned char *>(value);
    size_t i = 0;
    while (i < length) {
        const unsigned char lead = bytes[i];
        if (lead < 0x20U || lead == 0x7FU) return false;
        if (lead < 0x80U) {
            ++i;
            continue;
        }
        const size_t remaining = length - i;
        if (lead >= 0xC2U && lead <= 0xDFU) {
            if (!valid_continuation(bytes + i, remaining, 2)) return false;
            i += 2;
        } else if (lead == 0xE0U) {
            if (!valid_continuation(bytes + i, remaining, 3) ||
                bytes[i + 1] < 0xA0U || bytes[i + 1] > 0xBFU) return false;
            i += 3;
        } else if ((lead >= 0xE1U && lead <= 0xECU) ||
                   (lead >= 0xEEU && lead <= 0xEFU)) {
            if (!valid_continuation(bytes + i, remaining, 3)) return false;
            i += 3;
        } else if (lead == 0xEDU) {
            if (!valid_continuation(bytes + i, remaining, 3) ||
                bytes[i + 1] < 0x80U || bytes[i + 1] > 0x9FU) return false;
            i += 3;
        } else if (lead == 0xF0U) {
            if (!valid_continuation(bytes + i, remaining, 4) ||
                bytes[i + 1] < 0x90U || bytes[i + 1] > 0xBFU) return false;
            i += 4;
        } else if (lead >= 0xF1U && lead <= 0xF3U) {
            if (!valid_continuation(bytes + i, remaining, 4)) return false;
            i += 4;
        } else if (lead == 0xF4U) {
            if (!valid_continuation(bytes + i, remaining, 4) ||
                bytes[i + 1] < 0x80U || bytes[i + 1] > 0x8FU) return false;
            i += 4;
        } else {
            return false;
        }
    }
    return true;
}

void echodesk_notifications_init() {
    if (!items) {
        items = static_cast<EchoDeskNotificationItem *>(
            calloc(ECHODESK_NOTIFICATION_CAPACITY,
                   sizeof(EchoDeskNotificationItem)));
    } else {
        memset(items, 0,
               ECHODESK_NOTIFICATION_CAPACITY * sizeof(EchoDeskNotificationItem));
    }
    item_count = 0;
    notifications_enabled = false;
    revision = 1;
}

void echodesk_notifications_set_enabled(bool enabled) {
    if (notifications_enabled == enabled) return;
    notifications_enabled = enabled;
    if (!enabled && items) {
        memset(items, 0,
               ECHODESK_NOTIFICATION_CAPACITY * sizeof(EchoDeskNotificationItem));
        item_count = 0;
    }
    bump_revision();
}

EchoDeskNotificationResult echodesk_notifications_push(
    const EchoDeskNotificationInput &notification) {
    if (!notifications_enabled) return EchoDeskNotificationResult::NOT_ENABLED;
    if (!items) return EchoDeskNotificationResult::OUT_OF_MEMORY;
    if (notification.id == 0) return EchoDeskNotificationResult::INVALID_ID;
    if (!echodesk_notification_text_valid(
            notification.source, ECHODESK_NOTIFICATION_SOURCE_MAX_BYTES, false)) {
        return EchoDeskNotificationResult::INVALID_SOURCE;
    }
    if (!echodesk_notification_text_valid(
            notification.title, ECHODESK_NOTIFICATION_TITLE_MAX_BYTES, false)) {
        return EchoDeskNotificationResult::INVALID_TITLE;
    }
    if (!echodesk_notification_text_valid(
            notification.body, ECHODESK_NOTIFICATION_BODY_MAX_BYTES, true)) {
        return EchoDeskNotificationResult::INVALID_BODY;
    }

    size_t existing = ECHODESK_NOTIFICATION_CAPACITY;
    for (size_t i = 0; i < item_count; ++i) {
        if (items[i].id == notification.id) {
            existing = i;
            break;
        }
    }
    if (existing < item_count) {
        for (size_t i = existing; i > 0; --i) items[i] = items[i - 1];
    } else {
        const size_t shift_count = item_count < ECHODESK_NOTIFICATION_CAPACITY
            ? item_count : ECHODESK_NOTIFICATION_CAPACITY - 1;
        for (size_t i = shift_count; i > 0; --i) items[i] = items[i - 1];
        if (item_count < ECHODESK_NOTIFICATION_CAPACITY) ++item_count;
    }

    EchoDeskNotificationItem &stored = items[0];
    memset(&stored, 0, sizeof(stored));
    stored.id = notification.id;
    stored.timestamp_epoch_seconds = notification.timestamp_epoch_seconds;
    copy_text(stored.source, sizeof(stored.source), notification.source);
    copy_text(stored.title, sizeof(stored.title), notification.title);
    copy_text(stored.body, sizeof(stored.body), notification.body);
    bump_revision();
    return EchoDeskNotificationResult::OK;
}

EchoDeskNotificationResult echodesk_notifications_dismiss(uint32_t id) {
    if (id == 0) return EchoDeskNotificationResult::INVALID_ID;
    for (size_t i = 0; i < item_count; ++i) {
        if (items[i].id != id) continue;
        for (size_t j = i; j + 1 < item_count; ++j) items[j] = items[j + 1];
        --item_count;
        memset(&items[item_count], 0, sizeof(items[item_count]));
        bump_revision();
        return EchoDeskNotificationResult::OK;
    }
    return EchoDeskNotificationResult::NOT_FOUND;
}

void echodesk_notifications_clear() {
    if (item_count == 0 || !items) return;
    memset(items, 0,
           ECHODESK_NOTIFICATION_CAPACITY * sizeof(EchoDeskNotificationItem));
    item_count = 0;
    bump_revision();
}

void echodesk_notifications_capture(EchoDeskNotificationSnapshot &out) {
    out.revision = revision;
    out.enabled = notifications_enabled;
    out.count = item_count;
    memset(out.items, 0, sizeof(out.items));
    if (items) {
        for (size_t i = 0; i < item_count; ++i) out.items[i] = items[i];
    }
}

const char *echodesk_notification_result_message(EchoDeskNotificationResult result) {
    switch (result) {
        case EchoDeskNotificationResult::OK: return "accepted";
        case EchoDeskNotificationResult::NOT_ENABLED: return "notification stack is not enabled";
        case EchoDeskNotificationResult::INVALID_ID: return "notification id must be nonzero";
        case EchoDeskNotificationResult::INVALID_SOURCE: return "notification source is invalid";
        case EchoDeskNotificationResult::INVALID_TITLE: return "notification title is invalid";
        case EchoDeskNotificationResult::INVALID_BODY: return "notification body is invalid";
        case EchoDeskNotificationResult::NOT_FOUND: return "notification was not found";
        case EchoDeskNotificationResult::OUT_OF_MEMORY: return "notification storage is unavailable";
    }
    return "notification request is invalid";
}
