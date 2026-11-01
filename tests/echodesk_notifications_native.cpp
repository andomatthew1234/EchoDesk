#include "../src/echodesk_notifications.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static EchoDeskNotificationResult push(uint32_t id, const char *source,
                                       const char *title, const char *body,
                                       uint64_t timestamp = 0) {
    EchoDeskNotificationInput input{};
    input.id = id;
    input.source = source;
    input.title = title;
    input.body = body;
    input.timestamp_epoch_seconds = timestamp;
    return echodesk_notifications_push(input);
}

int main() {
    EchoDeskNotificationSnapshot snapshot{};
    echodesk_notifications_init();
    echodesk_notifications_capture(snapshot);
    assert(snapshot.revision == 1);
    assert(!snapshot.enabled);
    assert(snapshot.count == 0);
    assert(push(1, "Mail", "Hello", "Body") ==
           EchoDeskNotificationResult::NOT_ENABLED);

    echodesk_notifications_set_enabled(true);
    assert(push(1, "Mail", "Hello", "First", 100) ==
           EchoDeskNotificationResult::OK);
    assert(push(2, "Chat", "Reply", "Second", 200) ==
           EchoDeskNotificationResult::OK);
    echodesk_notifications_capture(snapshot);
    assert(snapshot.enabled);
    assert(snapshot.count == 2);
    assert(snapshot.items[0].id == 2);
    assert(strcmp(snapshot.items[0].source, "Chat") == 0);
    assert(snapshot.items[0].timestamp_epoch_seconds == 200);

    // Reusing an ID updates and promotes the existing item without growing the
    // queue, which lets hosts amend an OS notification safely.
    assert(push(1, "Mail", "Updated", "Changed", 300) ==
           EchoDeskNotificationResult::OK);
    echodesk_notifications_capture(snapshot);
    assert(snapshot.count == 2);
    assert(snapshot.items[0].id == 1);
    assert(strcmp(snapshot.items[0].title, "Updated") == 0);

    for (uint32_t id = 3; id <= 10; ++id) {
        assert(push(id, "System", "Event", "Message", id) ==
               EchoDeskNotificationResult::OK);
    }
    echodesk_notifications_capture(snapshot);
    assert(snapshot.count == ECHODESK_NOTIFICATION_CAPACITY);
    assert(snapshot.items[0].id == 10);
    assert(snapshot.items[ECHODESK_NOTIFICATION_CAPACITY - 1].id == 3);

    assert(echodesk_notifications_dismiss(6) == EchoDeskNotificationResult::OK);
    assert(echodesk_notifications_dismiss(6) == EchoDeskNotificationResult::NOT_FOUND);
    echodesk_notifications_capture(snapshot);
    assert(snapshot.count == ECHODESK_NOTIFICATION_CAPACITY - 1);
    for (size_t i = 0; i < snapshot.count; ++i) assert(snapshot.items[i].id != 6);

    const uint32_t revision_before_invalid = snapshot.revision;
    assert(push(0, "System", "Event", "Message") ==
           EchoDeskNotificationResult::INVALID_ID);
    assert(push(20, "", "Event", "Message") ==
           EchoDeskNotificationResult::INVALID_SOURCE);
    assert(push(20, "System", "", "Message") ==
           EchoDeskNotificationResult::INVALID_TITLE);
    assert(push(20, "System", "Event", "bad\nbody") ==
           EchoDeskNotificationResult::INVALID_BODY);
    const char invalid_utf8[] = {static_cast<char>(0xC0), static_cast<char>(0xAF), 0};
    assert(push(20, "System", "Event", invalid_utf8) ==
           EchoDeskNotificationResult::INVALID_BODY);
    echodesk_notifications_capture(snapshot);
    assert(snapshot.revision == revision_before_invalid);

    const char unicode_body[] = "All good \xE2\x9C\x93";
    assert(push(20, "System", "Unicode", unicode_body) ==
           EchoDeskNotificationResult::OK);

    echodesk_notifications_clear();
    echodesk_notifications_capture(snapshot);
    assert(snapshot.count == 0);
    echodesk_notifications_set_enabled(false);
    echodesk_notifications_capture(snapshot);
    assert(!snapshot.enabled);
    assert(snapshot.count == 0);

    puts("echodesk_notifications_native: PASS");
    return 0;
}
