#pragma once

// Itemzflow concatenates "/mnt/usbN/" + "/itemzflow/...",
// yielding an optional double slash. POSIX resolves both variants,
// but readlink() preserves the exact string originally written.
// Only repeated path separators are normalized: no dot-dot or
// symlink traversal is done by this comparison helper.
static inline bool usbfix_same_path(const char* a, const char* b) {
    if (!a || !b) return false;
    for (;;) {
        while (a[0] == '/' && a[1] == '/') ++a;
        while (b[0] == '/' && b[1] == '/') ++b;
        if (*a != *b) return false;
        if (*a == '\0') return true;
        ++a;
        ++b;
    }
}
