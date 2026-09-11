/* SPDX-License-Identifier: MIT */
#include <ble_mouse/hid_parser.h>

#include <errno.h>
#include <limits.h>
#include <string.h>

#define STACK_DEPTH 8
#define LOCAL_USAGES 32

struct global_state {
    uint32_t page, size, count;
    int32_t minimum;
    uint8_t id;
};

struct local_state {
    uint32_t usages[LOCAL_USAGES];
    uint32_t minimum, maximum;
    uint8_t count;
    bool has_minimum, has_maximum;
};

static uint32_t unsigned_item(const uint8_t *data, uint8_t size) {
    uint32_t result = 0;
    for (uint8_t i = 0; i < size; i++) {
        result |= (uint32_t)data[i] << (8 * i);
    }
    return result;
}

static int32_t signed_bits(uint32_t value, uint8_t bits) {
    if (bits == 32) {
        return (int32_t)value;
    }
    uint32_t sign = UINT32_C(1) << (bits - 1);
    return (int32_t)(value ^ sign) - (int32_t)sign;
}

static uint32_t local_usage(const struct local_state *local, uint32_t index) {
    if (index < local->count) {
        return local->usages[index];
    }
    if (local->has_minimum && local->has_maximum &&
        local->maximum >= local->minimum && index <= local->maximum - local->minimum) {
        return local->minimum + index;
    }
    return local->count ? local->usages[local->count - 1] : UINT32_MAX;
}

int mouse_hid_report_index(const struct mouse_hid_map *map, uint8_t id) {
    for (uint8_t i = 0; i < map->report_count; i++) {
        if (map->reports[i].id == id) {
            return i;
        }
    }
    return -ENOENT;
}

static int add_input(struct mouse_hid_map *map, const struct global_state *global,
                     const struct local_state *local, uint32_t flags, bool in_mouse) {
    if (!global->count || !global->size || global->size > 32 ||
        global->count > (MOUSE_HID_MAX_REPORT_BYTES * 8) / global->size) {
        return -E2BIG;
    }
    int report_index = mouse_hid_report_index(map, global->id);
    if (report_index < 0) {
        if (map->report_count == MOUSE_HID_MAX_REPORTS) {
            return -E2BIG;
        }
        report_index = map->report_count++;
        map->reports[report_index].id = global->id;
    }
    struct mouse_hid_report *report = &map->reports[report_index];
    uint32_t bits = global->size * global->count;
    if (bits > MOUSE_HID_MAX_REPORT_BYTES * 8u - report->bits) {
        return -E2BIG;
    }
    uint16_t start = report->bits;
    report->bits += bits;
    /* Constant and array fields still contribute to the input report offset. */
    if ((flags & 1) || !(flags & 2)) {
        return 0;
    }
    for (uint32_t i = 0; i < global->count; i++) {
        uint32_t usage = local_usage(local, i);
        uint16_t page = usage >> 16;
        uint16_t code = usage & 0xffff;
        struct mouse_hid_field field = {
            .bit = start + i * global->size,
            .size = global->size,
            .report = report_index,
            .is_signed = global->minimum < 0,
        };
        if (in_mouse && page == 9 && code >= 1 && code <= 5) {
            field.usage = MOUSE_BUTTON;
            field.button = code - 1;
        } else if (in_mouse && page == 1 && (flags & 4) && code == 0x30) {
            field.usage = MOUSE_X;
            map->x = true;
        } else if (in_mouse && page == 1 && (flags & 4) && code == 0x31) {
            field.usage = MOUSE_Y;
            map->y = true;
        } else if (in_mouse && page == 1 && (flags & 4) && code == 0x38) {
            field.usage = MOUSE_WHEEL;
            map->wheel = true;
        } else if (page == 0x0c && (flags & 4) && code == 0x238) {
            field.usage = MOUSE_PAN;
            map->pan = true;
        } else if (page == 0x0c && (code == 0x224 || code == 0x225)) {
            field.usage = MOUSE_BUTTON;
            field.button = code == 0x224 ? 3 : 4;
        } else {
            continue;
        }
        if (map->field_count == MOUSE_HID_MAX_FIELDS) {
            return -E2BIG;
        }
        map->fields[map->field_count++] = field;
        report->useful = true;
        if (field.usage == MOUSE_BUTTON) {
            report->buttons |= 1u << field.button;
            map->buttons |= 1u << field.button;
        }
    }
    return 0;
}

int mouse_hid_parse(const uint8_t *data, size_t length, struct mouse_hid_map *map) {
    if (!data || !map || !length || length > MOUSE_HID_MAX_MAP_BYTES) {
        return -EINVAL;
    }
    memset(map, 0, sizeof(*map));
    struct global_state global = {0}, stack[STACK_DEPTH];
    struct local_state local = {0};
    bool collections[STACK_DEPTH] = {0};
    unsigned depth = 0, stack_size = 0;
    for (size_t offset = 0; offset < length;) {
        uint8_t prefix = data[offset++];
        if (prefix == 0xfe) {
            if (length - offset < 2 || data[offset] > length - offset - 2) {
                return -EINVAL;
            }
            offset += 2 + data[offset];
            continue;
        }
        uint8_t size = prefix & 3;
        size = size == 3 ? 4 : size;
        if (size > length - offset) {
            return -EINVAL;
        }
        uint32_t value = unsigned_item(data + offset, size);
        offset += size;
        uint8_t type = (prefix >> 2) & 3;
        uint8_t tag = prefix >> 4;
        if (type == 1) {
            switch (tag) {
            case 0:
                if (value > 0xffff) {
                    return -EINVAL;
                }
                global.page = value;
                break;
            case 1:
                if (!size) {
                    return -EINVAL;
                }
                global.minimum = signed_bits(value, size * 8);
                break;
            case 7: global.size = value; break;
            case 8:
                if (!value || value > 255) {
                    return -EINVAL;
                }
                global.id = value;
                break;
            case 9: global.count = value; break;
            case 10:
                if (stack_size == STACK_DEPTH) {
                    return -E2BIG;
                }
                stack[stack_size++] = global;
                break;
            case 11:
                if (!stack_size) {
                    return -EINVAL;
                }
                global = stack[--stack_size];
                break;
            }
        } else if (type == 2) {
            uint32_t usage = size == 4 ? value : (global.page << 16) | value;
            switch (tag) {
            case 0:
                if (local.count == LOCAL_USAGES) {
                    return -E2BIG;
                }
                local.usages[local.count++] = usage;
                break;
            case 1: local.minimum = usage; local.has_minimum = true; break;
            case 2: local.maximum = usage; local.has_maximum = true; break;
            /* Alternative usage sets are not supported. */
            case 10: return -ENOTSUP;
            }
        } else if (type == 0) {
            if (tag == 10) {
                if (depth == STACK_DEPTH) {
                    return -E2BIG;
                }
                bool mouse = depth && collections[depth - 1];
                if (value == 1) {
                    mouse = local_usage(&local, 0) == 0x00010002;
                }
                collections[depth++] = mouse;
            } else if (tag == 12) {
                if (!depth) {
                    return -EINVAL;
                }
                depth--;
            } else if (tag == 8) {
                int err = add_input(map, &global, &local, value,
                                    depth && collections[depth - 1]);
                if (err) {
                    return err;
                }
            }
            memset(&local, 0, sizeof(local));
        }
    }
    if (depth || stack_size) {
        return -EINVAL;
    }
    return map->x && map->y ? 0 : -ENOTSUP;
}

static int32_t extract_field(const uint8_t *data, const struct mouse_hid_field *field) {
    uint32_t value = 0;
    for (uint8_t i = 0; i < field->size; i++) {
        unsigned bit = field->bit + i;
        value |= (uint32_t)((data[bit / 8] >> (bit % 8)) & 1) << i;
    }
    return field->is_signed ? signed_bits(value, field->size) :
           (value > INT32_MAX ? INT32_MAX : (int32_t)value);
}

static int32_t add_saturated(int32_t a, int32_t b) {
    int64_t result = (int64_t)a + b;
    return result > INT32_MAX ? INT32_MAX : result < INT32_MIN ? INT32_MIN : (int32_t)result;
}

int mouse_hid_decode(const struct mouse_hid_map *map, uint8_t id, const uint8_t *data,
                     size_t length, struct mouse_hid_frame *frame) {
    if (!map || !data || !frame) {
        return -EINVAL;
    }
    int index = mouse_hid_report_index(map, id);
    if (index < 0 || !map->reports[index].useful) {
        return -ENOENT;
    }
    if (length != (map->reports[index].bits + 7u) / 8u) {
        return -EMSGSIZE;
    }
    memset(frame, 0, sizeof(*frame));
    frame->buttons_present = map->reports[index].buttons;
    for (uint8_t i = 0; i < map->field_count; i++) {
        const struct mouse_hid_field *field = &map->fields[i];
        if (field->report != index) {
            continue;
        }
        int32_t value = extract_field(data, field);
        switch (field->usage) {
        case MOUSE_X: frame->x = add_saturated(frame->x, value); break;
        case MOUSE_Y: frame->y = add_saturated(frame->y, value); break;
        case MOUSE_WHEEL: frame->wheel = add_saturated(frame->wheel, value); break;
        case MOUSE_PAN: frame->pan = add_saturated(frame->pan, value); break;
        case MOUSE_BUTTON:
            if (value) {
                frame->buttons |= 1u << field->button;
            }
            break;
        }
    }
    return 0;
}
