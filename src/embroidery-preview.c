#include <errno.h>
#include <limits.h>
#include <stdckdint.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

constexpr size_t MAX_INPUT_FILE_SIZE = 67108864U;
constexpr size_t MAX_STITCH_SEGMENTS = 16777216U;
constexpr size_t INITIAL_SEGMENT_CAPACITY_CAP = 262144U;
constexpr uint32_t MIN_OUTPUT_DIMENSION = 64U;
constexpr uint32_t MAX_OUTPUT_DIMENSION = 4096U;
constexpr uint32_t DEFAULT_OUTPUT_DIMENSION = 512U;
constexpr uint32_t CANVAS_PADDING_PIXELS = 12U;
constexpr size_t JEF_HEADER_SIZE = 116U;
constexpr size_t PEC_HEADER_SIZE = 532U;
constexpr size_t DST_HEADER_SIZE = 512U;
constexpr size_t JANOME_PALETTE_COUNT = 79U;
constexpr size_t BROTHER_PALETTE_COUNT = 65U;
constexpr size_t DST_DEFAULT_PALETTE_COUNT = 8U;
constexpr size_t PNG_MAX_CHUNK_LENGTH = 0x7FFFFFFFU;

/* Bound coordinates to +/-10 km in 0.1 mm stitch units to prevent overflow. */
constexpr int64_t MAX_COORDINATE_MAGNITUDE = 100000000LL;

typedef enum : uint8_t {
    FORMAT_UNKNOWN = 0U,
    FORMAT_JEF = 1U,
    FORMAT_PES = 2U,
    FORMAT_PEC = 3U,
    FORMAT_DST = 4U
} EmbroideryFormat;

typedef struct {
    uint8_t red;
    uint8_t green;
    uint8_t blue;
} RgbColor;

typedef struct {
    int32_t start_x;
    int32_t start_y;
    int32_t end_x;
    int32_t end_y;
    RgbColor color;
} StitchSegment;

typedef struct {
    StitchSegment *segments;
    size_t segment_count;
    size_t segment_capacity;
    int64_t current_x;
    int64_t current_y;
    int64_t minimum_x;
    int64_t maximum_x;
    int64_t minimum_y;
    int64_t maximum_y;
    bool has_bounds;
    bool pen_is_down;
} StitchCanvas;

/* Standard Janome JEF thread palette for indices 0..78. */
static const RgbColor janome_palette[JANOME_PALETTE_COUNT] = {
    {0x00U, 0x00U, 0x00U}, {0x00U, 0x00U, 0x00U}, {0xFFU, 0xFFU, 0xFFU},
    {0xFFU, 0xFFU, 0x17U}, {0xFFU, 0x66U, 0x00U}, {0x2FU, 0x59U, 0x33U},
    {0x23U, 0x73U, 0x36U}, {0x65U, 0xC2U, 0xC8U}, {0xABU, 0x5AU, 0x96U},
    {0xF6U, 0x69U, 0xA0U}, {0xFFU, 0x00U, 0x00U}, {0xB1U, 0x70U, 0x4EU},
    {0x0BU, 0x2FU, 0x84U}, {0xE4U, 0xC3U, 0x5DU}, {0x48U, 0x1AU, 0x05U},
    {0xACU, 0x9CU, 0xC7U}, {0xFCU, 0xF2U, 0x94U}, {0xF9U, 0x99U, 0xB7U},
    {0xFAU, 0xB3U, 0x81U}, {0xC9U, 0xA4U, 0x80U}, {0x97U, 0x05U, 0x33U},
    {0xA0U, 0xB8U, 0xCCU}, {0x7FU, 0xC2U, 0x1CU}, {0xE5U, 0xE5U, 0xE5U},
    {0x88U, 0x9BU, 0x9BU}, {0x98U, 0xD6U, 0xBDU}, {0xB2U, 0xE1U, 0xE3U},
    {0x36U, 0x8BU, 0xA0U}, {0x4FU, 0x83U, 0xABU}, {0x38U, 0x6AU, 0x91U},
    {0x07U, 0x16U, 0x50U}, {0xF9U, 0x99U, 0xA2U}, {0xF9U, 0x67U, 0x6BU},
    {0xE3U, 0x31U, 0x1FU}, {0xE2U, 0xA1U, 0x88U}, {0xB5U, 0x94U, 0x74U},
    {0xE4U, 0xCFU, 0x99U}, {0xFFU, 0xCBU, 0x00U}, {0xE1U, 0xADU, 0xD4U},
    {0xC3U, 0x00U, 0x7EU}, {0x80U, 0x00U, 0x4BU}, {0x54U, 0x05U, 0x71U},
    {0xB1U, 0x05U, 0x25U}, {0xCAU, 0xE0U, 0xC0U}, {0x89U, 0x98U, 0x56U},
    {0x5CU, 0x94U, 0x1AU}, {0x00U, 0x31U, 0x14U}, {0x5DU, 0xAEU, 0x94U},
    {0x4CU, 0xBFU, 0x8FU}, {0x00U, 0x77U, 0x72U}, {0x59U, 0x5BU, 0x61U},
    {0xFFU, 0xFFU, 0xF2U}, {0xB1U, 0x58U, 0x18U}, {0xCBU, 0x8AU, 0x07U},
    {0x98U, 0x6CU, 0x80U}, {0x98U, 0x69U, 0x2DU}, {0x4DU, 0x34U, 0x19U},
    {0x4CU, 0x33U, 0x0BU}, {0x33U, 0x20U, 0x0AU}, {0x52U, 0x3AU, 0x97U},
    {0x0DU, 0x21U, 0x7EU}, {0x1EU, 0x77U, 0xACU}, {0xB2U, 0xDDU, 0x53U},
    {0xF3U, 0x36U, 0x89U}, {0xDEU, 0x64U, 0x9EU}, {0x98U, 0x41U, 0x61U},
    {0x4CU, 0x56U, 0x12U}, {0x4CU, 0x88U, 0x1FU}, {0xE4U, 0xDEU, 0x79U},
    {0xCBU, 0x8AU, 0x1AU}, {0xCBU, 0xA2U, 0x1CU}, {0xFFU, 0x98U, 0x05U},
    {0xFCU, 0xB2U, 0x57U}, {0xFFU, 0xE5U, 0x05U}, {0xF0U, 0x33U, 0x1FU},
    {0x1AU, 0x84U, 0x2DU}, {0x38U, 0x6CU, 0xAEU}, {0xE3U, 0xC4U, 0xB4U},
    {0xE3U, 0xACU, 0x81U}
};

/* Standard Brother PEC/PES thread palette for indices 0..64. */
static const RgbColor brother_palette[BROTHER_PALETTE_COUNT] = {
    {0x00U, 0x00U, 0x00U}, {0x0EU, 0x1FU, 0x7CU}, {0x0AU, 0x55U, 0xA3U},
    {0x00U, 0x87U, 0x77U}, {0x4BU, 0x6BU, 0xAFU}, {0xEDU, 0x17U, 0x1FU},
    {0xD1U, 0x5CU, 0x00U}, {0x91U, 0x36U, 0x97U}, {0xE4U, 0x9AU, 0xCBU},
    {0x91U, 0x5FU, 0xACU}, {0x9EU, 0xD6U, 0x7DU}, {0xE8U, 0xA9U, 0x00U},
    {0xFEU, 0xBAU, 0x35U}, {0xFFU, 0xFFU, 0x00U}, {0x70U, 0xBCU, 0x1FU},
    {0xBAU, 0x98U, 0x00U}, {0xA8U, 0xA8U, 0xA8U}, {0x7DU, 0x6FU, 0x00U},
    {0xFFU, 0xFFU, 0xB3U}, {0x4FU, 0x55U, 0x56U}, {0x00U, 0x00U, 0x00U},
    {0x0BU, 0x3DU, 0x91U}, {0x77U, 0x01U, 0x76U}, {0x29U, 0x31U, 0x33U},
    {0x2AU, 0x13U, 0x01U}, {0xF6U, 0x4AU, 0x8AU}, {0xB2U, 0x76U, 0x24U},
    {0xFCU, 0xBBU, 0xC5U}, {0xFEU, 0x37U, 0x0FU}, {0xF0U, 0xF0U, 0xF0U},
    {0x6AU, 0x1CU, 0x8AU}, {0xA8U, 0xDDU, 0xC4U}, {0x25U, 0x84U, 0xBBU},
    {0xFEU, 0xB3U, 0x43U}, {0xFFU, 0xF3U, 0x6BU}, {0xD0U, 0xA6U, 0x60U},
    {0xD1U, 0x54U, 0x00U}, {0x66U, 0xBAU, 0x49U}, {0x13U, 0x4AU, 0x46U},
    {0x87U, 0x87U, 0x87U}, {0xD8U, 0xCCU, 0xC6U}, {0x43U, 0x56U, 0x07U},
    {0xFDU, 0xD9U, 0xDEU}, {0xF9U, 0x93U, 0xBCU}, {0x00U, 0x38U, 0x22U},
    {0xB2U, 0xAFU, 0xD4U}, {0x68U, 0x6AU, 0xB0U}, {0xEFU, 0xE3U, 0xB9U},
    {0xF7U, 0x38U, 0x66U}, {0xB5U, 0x4BU, 0x64U}, {0x13U, 0x2BU, 0x1AU},
    {0xC7U, 0x01U, 0x56U}, {0xFEU, 0x9EU, 0x32U}, {0xA8U, 0xDEU, 0xEBU},
    {0x00U, 0x67U, 0x3EU}, {0x4EU, 0x29U, 0x90U}, {0x2FU, 0x7EU, 0x20U},
    {0xFFU, 0xCCU, 0xCCU}, {0xFFU, 0xD9U, 0x11U}, {0x09U, 0x5BU, 0xA6U},
    {0xF0U, 0xF9U, 0x70U}, {0xE3U, 0xF3U, 0x5BU}, {0xFFU, 0x99U, 0x00U},
    {0xFFU, 0xF0U, 0x8DU}, {0xFFU, 0xC8U, 0xC8U}
};

/* High-contrast default thread palette for Tajima DST files without color tables. */
static const RgbColor dst_default_palette[DST_DEFAULT_PALETTE_COUNT] = {
    {0x1EU, 0x1EU, 0x1EU}, {0xC8U, 0x28U, 0x28U}, {0x1EU, 0x5AU, 0xAAU},
    {0xF0U, 0xB4U, 0x1EU}, {0x28U, 0x8CU, 0x46U}, {0x82U, 0x3CU, 0x96U},
    {0xE6U, 0x78U, 0x1EU}, {0x5AU, 0x5AU, 0x5AU}
};

static uint32_t read_uint32_little_endian(const uint8_t *bytes, size_t offset) {
    const uint32_t byte_zero = (uint32_t)bytes[offset];
    const uint32_t byte_one = (uint32_t)bytes[offset + 1U] << 8;
    const uint32_t byte_two = (uint32_t)bytes[offset + 2U] << 16;
    const uint32_t byte_three = (uint32_t)bytes[offset + 3U] << 24;
    return byte_zero | byte_one | byte_two | byte_three;
}

static void write_uint32_big_endian(uint8_t *destination, size_t offset, uint32_t value) {
    destination[offset] = (uint8_t)((value >> 24) & 0xFFU);
    destination[offset + 1U] = (uint8_t)((value >> 16) & 0xFFU);
    destination[offset + 2U] = (uint8_t)((value >> 8) & 0xFFU);
    destination[offset + 3U] = (uint8_t)(value & 0xFFU);
}

static unsigned char ascii_lowercase(unsigned char character) {
    if (character >= 'A' && character <= 'Z') {
        return (unsigned char)(character - 'A' + 'a');
    }
    return character;
}

static bool strings_equal_ignore_case(const char *left_text, const char *right_text) {
    size_t character_index = 0U;
    while (left_text[character_index] != '\0' && right_text[character_index] != '\0') {
        const unsigned char left_character =
            ascii_lowercase((unsigned char)left_text[character_index]);
        const unsigned char right_character =
            ascii_lowercase((unsigned char)right_text[character_index]);
        if (left_character != right_character) {
            return false;
        }
        character_index += 1U;
    }
    return left_text[character_index] == '\0' && right_text[character_index] == '\0';
}

static EmbroideryFormat detect_embroidery_format(
    const char *input_path,
    const uint8_t *file_buffer,
    size_t file_size
) {
    const char *last_slash = strrchr(input_path, '/');
    const char *file_name = input_path;
    if (last_slash != nullptr) {
        file_name = last_slash + 1;
    }

    const char *last_dot = strrchr(file_name, '.');
    if (last_dot != nullptr && last_dot[1] != '\0') {
        const char *extension = last_dot + 1;
        if (strings_equal_ignore_case(extension, "jef") ||
            strings_equal_ignore_case(extension, "jef+")) {
            return FORMAT_JEF;
        }
        if (strings_equal_ignore_case(extension, "pes")) {
            return FORMAT_PES;
        }
        if (strings_equal_ignore_case(extension, "pec")) {
            return FORMAT_PEC;
        }
        if (strings_equal_ignore_case(extension, "dst")) {
            return FORMAT_DST;
        }
    }

    if (file_size >= 4U && memcmp(file_buffer, "#PES", 4U) == 0) {
        return FORMAT_PES;
    }
    if (file_size >= 4U && memcmp(file_buffer, "#PEC", 4U) == 0) {
        return FORMAT_PEC;
    }
    return FORMAT_UNKNOWN;
}

static bool initialize_stitch_canvas(StitchCanvas *canvas, size_t estimated_segments) {
    *canvas = (StitchCanvas){};

    size_t initial_capacity = estimated_segments;
    if (initial_capacity == 0U) {
        initial_capacity = 1024U;
    }
    if (initial_capacity > INITIAL_SEGMENT_CAPACITY_CAP) {
        initial_capacity = INITIAL_SEGMENT_CAPACITY_CAP;
    }

    size_t allocation_bytes = 0U;
    if (ckd_mul(&allocation_bytes, initial_capacity, sizeof(StitchSegment))) {
        fprintf(stderr, "Stitch buffer capacity overflow\n");
        return false;
    }

    canvas->segments = (StitchSegment *)malloc(allocation_bytes);
    if (canvas->segments == nullptr) {
        fprintf(stderr, "Failed to allocate stitch array\n");
        return false;
    }
    canvas->segment_capacity = initial_capacity;
    return true;
}

static bool record_canvas_move(StitchCanvas *canvas, int64_t delta_x, int64_t delta_y) {
    int64_t next_x = 0;
    int64_t next_y = 0;
    if (ckd_add(&next_x, canvas->current_x, delta_x) ||
        ckd_add(&next_y, canvas->current_y, delta_y) ||
        next_x < -MAX_COORDINATE_MAGNITUDE || next_x > MAX_COORDINATE_MAGNITUDE ||
        next_y < -MAX_COORDINATE_MAGNITUDE || next_y > MAX_COORDINATE_MAGNITUDE) {
        fprintf(stderr, "Stitch coordinates out of valid range\n");
        return false;
    }

    canvas->current_x = next_x;
    canvas->current_y = next_y;
    canvas->pen_is_down = false;
    return true;
}

static bool grow_stitch_canvas(StitchCanvas *canvas) {
    if (canvas->segment_capacity >= MAX_STITCH_SEGMENTS) {
        fprintf(stderr, "Stitch count exceeds safety limit\n");
        return false;
    }

    size_t next_capacity = 0U;
    if (ckd_mul(&next_capacity, canvas->segment_capacity, 2U) ||
        next_capacity > MAX_STITCH_SEGMENTS) {
        next_capacity = MAX_STITCH_SEGMENTS;
    }

    size_t allocation_bytes = 0U;
    if (ckd_mul(&allocation_bytes, next_capacity, sizeof(StitchSegment))) {
        fprintf(stderr, "Stitch array allocation size overflow\n");
        return false;
    }

    StitchSegment *grown_segments = (StitchSegment *)realloc(canvas->segments, allocation_bytes);
    if (grown_segments == nullptr) {
        fprintf(stderr, "Failed to grow stitch array\n");
        return false;
    }

    canvas->segments = grown_segments;
    canvas->segment_capacity = next_capacity;
    return true;
}

static bool record_canvas_stitch(
    StitchCanvas *canvas,
    int64_t delta_x,
    int64_t delta_y,
    RgbColor active_color
) {
    const int64_t previous_x = canvas->current_x;
    const int64_t previous_y = canvas->current_y;
    int64_t next_x = 0;
    int64_t next_y = 0;

    if (ckd_add(&next_x, previous_x, delta_x) ||
        ckd_add(&next_y, previous_y, delta_y) ||
        next_x < -MAX_COORDINATE_MAGNITUDE || next_x > MAX_COORDINATE_MAGNITUDE ||
        next_y < -MAX_COORDINATE_MAGNITUDE || next_y > MAX_COORDINATE_MAGNITUDE) {
        fprintf(stderr, "Stitch coordinates out of valid range\n");
        return false;
    }

    canvas->current_x = next_x;
    canvas->current_y = next_y;

    if (!canvas->has_bounds) {
        canvas->minimum_x = next_x;
        canvas->maximum_x = next_x;
        canvas->minimum_y = next_y;
        canvas->maximum_y = next_y;
        canvas->has_bounds = true;
    } else {
        if (next_x < canvas->minimum_x) {
            canvas->minimum_x = next_x;
        }
        if (next_x > canvas->maximum_x) {
            canvas->maximum_x = next_x;
        }
        if (next_y < canvas->minimum_y) {
            canvas->minimum_y = next_y;
        }
        if (next_y > canvas->maximum_y) {
            canvas->maximum_y = next_y;
        }
    }

    if (!canvas->pen_is_down) {
        canvas->pen_is_down = true;
        return true;
    }

    if (canvas->segment_count == canvas->segment_capacity) {
        if (!grow_stitch_canvas(canvas)) {
            return false;
        }
    }

    StitchSegment *destination = &canvas->segments[canvas->segment_count];
    destination->start_x = (int32_t)previous_x;
    destination->start_y = (int32_t)previous_y;
    destination->end_x = (int32_t)next_x;
    destination->end_y = (int32_t)next_y;
    destination->color = active_color;
    canvas->segment_count += 1U;
    return true;
}

static RgbColor resolve_janome_slot_color(
    const uint8_t *file_buffer,
    uint32_t color_count,
    uint32_t color_slot
) {
    if (color_count == 0U) {
        return janome_palette[1];
    }

    const uint32_t wrapped_slot = color_slot % color_count;
    const uint32_t raw_palette_word =
        read_uint32_little_endian(file_buffer, JEF_HEADER_SIZE + (size_t)wrapped_slot * 4U);
    int64_t signed_index = (int64_t)(int32_t)raw_palette_word;
    if (signed_index < 0) {
        signed_index = -signed_index;
    }
    const size_t palette_index = (size_t)(signed_index % (int64_t)JANOME_PALETTE_COUNT);
    return janome_palette[palette_index];
}

static RgbColor resolve_brother_slot_color(
    const uint8_t *file_buffer,
    size_t pec_offset,
    uint32_t color_count,
    uint32_t color_slot
) {
    if (color_count == 0U) {
        return brother_palette[20];
    }

    const uint32_t wrapped_slot = color_slot % color_count;
    const uint8_t raw_palette_index = file_buffer[pec_offset + 49U + (size_t)wrapped_slot];
    return brother_palette[(size_t)raw_palette_index % BROTHER_PALETTE_COUNT];
}

static bool parse_jef_file(
    const uint8_t *file_buffer,
    size_t file_size,
    StitchCanvas *canvas
) {
    if (file_size < JEF_HEADER_SIZE) {
        fprintf(stderr, "Invalid JEF file size\n");
        return false;
    }

    const uint32_t stitch_offset = read_uint32_little_endian(file_buffer, 0U);
    uint32_t color_count = read_uint32_little_endian(file_buffer, 24U);
    if (stitch_offset < JEF_HEADER_SIZE || (size_t)stitch_offset >= file_size) {
        fprintf(stderr, "Invalid JEF stitch offset\n");
        return false;
    }

    const size_t available_color_entries = ((size_t)stitch_offset - JEF_HEADER_SIZE) / 4U;
    if ((size_t)color_count > available_color_entries) {
        color_count = (uint32_t)available_color_entries;
    }

    const size_t estimated_segments = (file_size - (size_t)stitch_offset) / 2U;
    if (estimated_segments == 0U) {
        fprintf(stderr, "No stitch data in JEF file\n");
        return false;
    }
    if (!initialize_stitch_canvas(canvas, estimated_segments)) {
        return false;
    }

    uint32_t current_color_slot = 0U;
    RgbColor active_color = resolve_janome_slot_color(file_buffer, color_count, current_color_slot);
    size_t byte_offset = (size_t)stitch_offset;

    while (byte_offset + 1U < file_size) {
        const uint8_t first_byte = file_buffer[byte_offset];
        const uint8_t second_byte = file_buffer[byte_offset + 1U];
        byte_offset += 2U;

        if (first_byte != 0x80U) {
            const int64_t delta_x = (int64_t)(int8_t)first_byte;
            const int64_t delta_y = -(int64_t)(int8_t)second_byte;
            if (!record_canvas_stitch(canvas, delta_x, delta_y, active_color)) {
                return false;
            }
            continue;
        }

        /* 0x80 0x10 terminates stitch data before trailing JEF+ metadata. */
        if (second_byte == 0x10U || byte_offset + 1U >= file_size) {
            break;
        }

        const int64_t move_x = (int64_t)(int8_t)file_buffer[byte_offset];
        const int64_t move_y = -(int64_t)(int8_t)file_buffer[byte_offset + 1U];
        byte_offset += 2U;

        if (second_byte == 0x01U) {
            current_color_slot += 1U;
            active_color =
                resolve_janome_slot_color(file_buffer, color_count, current_color_slot);
            if (!record_canvas_move(canvas, move_x, move_y)) {
                return false;
            }
        } else if (second_byte == 0x02U || second_byte == 0x04U) {
            if (!record_canvas_move(canvas, move_x, move_y)) {
                return false;
            }
        } else if (second_byte == 0x80U) {
            canvas->pen_is_down = false;
        } else {
            break;
        }
    }

    return true;
}

static bool decode_pec_axis(
    const uint8_t *file_buffer,
    size_t file_size,
    size_t *byte_offset,
    int64_t *decoded_delta,
    bool *is_jump_or_trim
) {
    if (*byte_offset >= file_size) {
        return false;
    }

    const uint8_t lead_byte = file_buffer[*byte_offset];
    if ((lead_byte & 0x80U) != 0U) {
        if (*byte_offset + 1U >= file_size) {
            return false;
        }
        /* Bits 0x10 (jump) and 0x20 (trim) indicate non-stitch travel. */
        if ((lead_byte & 0x30U) != 0U) {
            *is_jump_or_trim = true;
        }
        const uint32_t high_nibble = (uint32_t)(lead_byte & 0x0FU) << 8;
        const uint32_t low_byte = (uint32_t)file_buffer[*byte_offset + 1U];
        const uint32_t combined_value = high_nibble | low_byte;
        if (combined_value >= 2048U) {
            *decoded_delta = (int64_t)combined_value - 4096LL;
        } else {
            *decoded_delta = (int64_t)combined_value;
        }
        *byte_offset += 2U;
        return true;
    }

    if (lead_byte >= 64U) {
        *decoded_delta = (int64_t)lead_byte - 128LL;
    } else {
        *decoded_delta = (int64_t)lead_byte;
    }
    *byte_offset += 1U;
    return true;
}

static bool parse_pec_section(
    const uint8_t *file_buffer,
    size_t file_size,
    size_t pec_offset,
    StitchCanvas *canvas
) {
    if (pec_offset >= file_size || file_size - pec_offset <= PEC_HEADER_SIZE) {
        fprintf(stderr, "PEC section is truncated or missing stitch data\n");
        return false;
    }

    /* Byte 48 stores (color_count - 1), and bytes 49..304 fit inside the 532-byte header. */
    const uint32_t color_count = (uint32_t)file_buffer[pec_offset + 48U] + 1U;
    size_t byte_offset = pec_offset + PEC_HEADER_SIZE;
    const size_t estimated_segments = (file_size - byte_offset) / 2U;
    if (!initialize_stitch_canvas(canvas, estimated_segments)) {
        return false;
    }

    uint32_t current_color_slot = 0U;
    RgbColor active_color =
        resolve_brother_slot_color(file_buffer, pec_offset, color_count, current_color_slot);

    while (byte_offset < file_size) {
        const uint8_t lead_byte = file_buffer[byte_offset];
        if (lead_byte == 0xFFU) {
            break;
        }

        if (lead_byte == 0xFEU) {
            if (byte_offset + 2U >= file_size || file_buffer[byte_offset + 1U] != 0xB0U) {
                break;
            }
            byte_offset += 3U;
            current_color_slot += 1U;
            active_color = resolve_brother_slot_color(
                file_buffer,
                pec_offset,
                color_count,
                current_color_slot
            );
            canvas->pen_is_down = false;
            continue;
        }

        int64_t delta_x = 0;
        int64_t delta_y = 0;
        bool is_jump_or_trim = false;
        if (!decode_pec_axis(file_buffer, file_size, &byte_offset, &delta_x, &is_jump_or_trim)) {
            break;
        }
        if (!decode_pec_axis(file_buffer, file_size, &byte_offset, &delta_y, &is_jump_or_trim)) {
            break;
        }

        if (is_jump_or_trim) {
            if (!record_canvas_move(canvas, delta_x, delta_y)) {
                return false;
            }
        } else {
            if (!record_canvas_stitch(canvas, delta_x, delta_y, active_color)) {
                return false;
            }
        }
    }

    return true;
}

static bool parse_pes_file(
    const uint8_t *file_buffer,
    size_t file_size,
    StitchCanvas *canvas
) {
    if (file_size < 12U || memcmp(file_buffer, "#PES", 4U) != 0) {
        fprintf(stderr, "Invalid PES header signature\n");
        return false;
    }

    const uint32_t pec_offset = read_uint32_little_endian(file_buffer, 8U);
    if ((size_t)pec_offset >= file_size || file_size - (size_t)pec_offset <= PEC_HEADER_SIZE) {
        fprintf(stderr, "Invalid PEC offset in PES file\n");
        return false;
    }

    return parse_pec_section(file_buffer, file_size, (size_t)pec_offset, canvas);
}

static bool parse_pec_file(
    const uint8_t *file_buffer,
    size_t file_size,
    StitchCanvas *canvas
) {
    size_t pec_offset = 0U;
    if (file_size >= 8U && memcmp(file_buffer, "#PEC", 4U) == 0) {
        pec_offset = 8U;
    } else if (file_size >= 3U && memcmp(file_buffer, "LA:", 3U) == 0) {
        pec_offset = 0U;
    } else {
        fprintf(stderr, "Invalid PEC header signature\n");
        return false;
    }

    return parse_pec_section(file_buffer, file_size, pec_offset, canvas);
}

static bool parse_dst_file(
    const uint8_t *file_buffer,
    size_t file_size,
    StitchCanvas *canvas
) {
    if (file_size < DST_HEADER_SIZE + 3U) {
        fprintf(stderr, "Invalid DST file size\n");
        return false;
    }

    const size_t estimated_segments = (file_size - DST_HEADER_SIZE) / 3U;
    if (!initialize_stitch_canvas(canvas, estimated_segments)) {
        return false;
    }

    uint32_t current_color_slot = 0U;
    RgbColor active_color = dst_default_palette[0];
    size_t byte_offset = DST_HEADER_SIZE;

    while (byte_offset + 2U < file_size) {
        const uint8_t first_byte = file_buffer[byte_offset];
        const uint8_t second_byte = file_buffer[byte_offset + 1U];
        const uint8_t third_byte = file_buffer[byte_offset + 2U];
        byte_offset += 3U;

        if ((third_byte & 0xF3U) == 0xF3U) {
            break;
        }

        /* Decode Tajima balanced-ternary delta and flip Y to top-down screen space. */
        int64_t delta_x = 0;
        int64_t delta_y = 0;

        if ((first_byte & 0x01U) != 0U) {
            delta_x += 1;
        }
        if ((first_byte & 0x02U) != 0U) {
            delta_x -= 1;
        }
        if ((first_byte & 0x04U) != 0U) {
            delta_x += 9;
        }
        if ((first_byte & 0x08U) != 0U) {
            delta_x -= 9;
        }
        if ((first_byte & 0x80U) != 0U) {
            delta_y -= 1;
        }
        if ((first_byte & 0x40U) != 0U) {
            delta_y += 1;
        }
        if ((first_byte & 0x20U) != 0U) {
            delta_y -= 9;
        }
        if ((first_byte & 0x10U) != 0U) {
            delta_y += 9;
        }

        if ((second_byte & 0x01U) != 0U) {
            delta_x += 3;
        }
        if ((second_byte & 0x02U) != 0U) {
            delta_x -= 3;
        }
        if ((second_byte & 0x04U) != 0U) {
            delta_x += 27;
        }
        if ((second_byte & 0x08U) != 0U) {
            delta_x -= 27;
        }
        if ((second_byte & 0x80U) != 0U) {
            delta_y -= 3;
        }
        if ((second_byte & 0x40U) != 0U) {
            delta_y += 3;
        }
        if ((second_byte & 0x20U) != 0U) {
            delta_y -= 27;
        }
        if ((second_byte & 0x10U) != 0U) {
            delta_y += 27;
        }

        if ((third_byte & 0x04U) != 0U) {
            delta_x += 81;
        }
        if ((third_byte & 0x08U) != 0U) {
            delta_x -= 81;
        }
        if ((third_byte & 0x20U) != 0U) {
            delta_y -= 81;
        }
        if ((third_byte & 0x10U) != 0U) {
            delta_y += 81;
        }

        if ((third_byte & 0xC3U) == 0xC3U) {
            current_color_slot += 1U;
            active_color = dst_default_palette[current_color_slot % DST_DEFAULT_PALETTE_COUNT];
            if (!record_canvas_move(canvas, delta_x, delta_y)) {
                return false;
            }
        } else if ((third_byte & 0x83U) == 0x83U) {
            if (!record_canvas_move(canvas, delta_x, delta_y)) {
                return false;
            }
        } else {
            if (!record_canvas_stitch(canvas, delta_x, delta_y, active_color)) {
                return false;
            }
        }
    }

    return true;
}

static int32_t clamp_pixel_coordinate(int32_t coordinate, uint32_t dimension) {
    if (coordinate < 0) {
        return 0;
    }
    const int32_t maximum_coordinate = (int32_t)dimension - 1;
    if (coordinate > maximum_coordinate) {
        return maximum_coordinate;
    }
    return coordinate;
}

static void draw_line(
    uint8_t *rgb_pixels,
    uint32_t image_width,
    uint32_t image_height,
    int32_t start_x,
    int32_t start_y,
    int32_t end_x,
    int32_t end_y,
    RgbColor color
) {
    int32_t delta_x = end_x - start_x;
    int32_t step_x = 1;
    if (delta_x < 0) {
        delta_x = -delta_x;
        step_x = -1;
    }

    int32_t delta_y = end_y - start_y;
    int32_t step_y = 1;
    if (delta_y < 0) {
        delta_y = -delta_y;
        step_y = -1;
    }
    delta_y = -delta_y;

    int32_t bresenham_error = delta_x + delta_y;
    int32_t current_x = start_x;
    int32_t current_y = start_y;

    while (true) {
        if (current_x >= 0 && current_x < (int32_t)image_width &&
            current_y >= 0 && current_y < (int32_t)image_height) {
            const size_t pixel_offset =
                ((size_t)current_y * (size_t)image_width + (size_t)current_x) * 3U;
            rgb_pixels[pixel_offset] = color.red;
            rgb_pixels[pixel_offset + 1U] = color.green;
            rgb_pixels[pixel_offset + 2U] = color.blue;
        }
        if (current_x == end_x && current_y == end_y) {
            break;
        }
        const int32_t double_error = bresenham_error * 2;
        if (double_error >= delta_y) {
            bresenham_error += delta_y;
            current_x += step_x;
        }
        if (double_error <= delta_x) {
            bresenham_error += delta_x;
            current_y += step_y;
        }
    }
}

static bool write_png_file(
    const char *output_path,
    const uint8_t *rgb_pixels,
    uint32_t image_width,
    uint32_t image_height
) {
    bool succeeded = false;
    FILE *output_file = nullptr;
    uint8_t *raw_scanlines = nullptr;
    uint8_t *compressed_buffer = nullptr;
    uint8_t *png_buffer = nullptr;

    if (image_width == 0U || image_height == 0U ||
        image_width > MAX_OUTPUT_DIMENSION || image_height > MAX_OUTPUT_DIMENSION) {
        fprintf(stderr, "Invalid PNG dimensions\n");
        goto cleanup;
    }

    size_t row_rgb_bytes = 0U;
    size_t scanline_bytes = 0U;
    size_t raw_payload_size = 0U;
    if (ckd_mul(&row_rgb_bytes, (size_t)image_width, 3U) ||
        ckd_add(&scanline_bytes, row_rgb_bytes, 1U) ||
        ckd_mul(&raw_payload_size, scanline_bytes, (size_t)image_height)) {
        fprintf(stderr, "PNG scanline buffer size overflow\n");
        goto cleanup;
    }
    if (raw_payload_size > (size_t)ULONG_MAX) {
        fprintf(stderr, "PNG payload exceeds zlib limit\n");
        goto cleanup;
    }

    raw_scanlines = (uint8_t *)malloc(raw_payload_size);
    if (raw_scanlines == nullptr) {
        fprintf(stderr, "Failed to allocate scanline buffer\n");
        goto cleanup;
    }

    for (size_t row_index = 0U; row_index < (size_t)image_height; row_index += 1U) {
        const size_t destination_offset = row_index * scanline_bytes;
        const size_t source_offset = row_index * row_rgb_bytes;
        raw_scanlines[destination_offset] = 0x00U;
        memcpy(
            raw_scanlines + destination_offset + 1U,
            rgb_pixels + source_offset,
            row_rgb_bytes
        );
    }

    const uLongf compressed_capacity = compressBound((uLong)raw_payload_size);
    if (compressed_capacity == 0UL || (size_t)compressed_capacity > PNG_MAX_CHUNK_LENGTH) {
        fprintf(stderr, "PNG compressed buffer size exceeds limit\n");
        goto cleanup;
    }

    compressed_buffer = (uint8_t *)malloc((size_t)compressed_capacity);
    if (compressed_buffer == nullptr) {
        fprintf(stderr, "Failed to allocate compressed buffer\n");
        goto cleanup;
    }

    uLongf compressed_size = compressed_capacity;
    const int zlib_status = compress2(
        compressed_buffer,
        &compressed_size,
        raw_scanlines,
        (uLong)raw_payload_size,
        Z_BEST_SPEED
    );
    if (zlib_status != Z_OK || compressed_size == 0UL ||
        (size_t)compressed_size > PNG_MAX_CHUNK_LENGTH) {
        fprintf(stderr, "Failed to compress PNG image data\n");
        goto cleanup;
    }

    const size_t idat_data_size = (size_t)compressed_size;
    size_t total_png_size = 0U;
    size_t idat_crc_length = 0U;
    if (ckd_add(&total_png_size, idat_data_size, 57U) ||
        ckd_add(&idat_crc_length, idat_data_size, 4U) ||
        idat_crc_length > (size_t)UINT_MAX) {
        fprintf(stderr, "PNG output size overflow\n");
        goto cleanup;
    }

    png_buffer = (uint8_t *)malloc(total_png_size);
    if (png_buffer == nullptr) {
        fprintf(stderr, "Failed to allocate PNG buffer\n");
        goto cleanup;
    }

    static const uint8_t png_signature[8] = {
        0x89U, 0x50U, 0x4EU, 0x47U, 0x0DU, 0x0AU, 0x1AU, 0x0AU
    };
    const uLong initial_crc = crc32(0UL, nullptr, 0U);
    size_t write_offset = 0U;

    memcpy(png_buffer + write_offset, png_signature, sizeof(png_signature));
    write_offset += sizeof(png_signature);

    write_uint32_big_endian(png_buffer, write_offset, 13U);
    write_offset += 4U;
    const size_t ihdr_type_offset = write_offset;
    memcpy(png_buffer + write_offset, "IHDR", 4U);
    write_offset += 4U;
    write_uint32_big_endian(png_buffer, write_offset, image_width);
    write_offset += 4U;
    write_uint32_big_endian(png_buffer, write_offset, image_height);
    write_offset += 4U;
    png_buffer[write_offset] = 8U;
    png_buffer[write_offset + 1U] = 2U;
    png_buffer[write_offset + 2U] = 0U;
    png_buffer[write_offset + 3U] = 0U;
    png_buffer[write_offset + 4U] = 0U;
    write_offset += 5U;
    const uint32_t ihdr_crc32 = (uint32_t)crc32(initial_crc, png_buffer + ihdr_type_offset, 17U);
    write_uint32_big_endian(png_buffer, write_offset, ihdr_crc32);
    write_offset += 4U;

    write_uint32_big_endian(png_buffer, write_offset, (uint32_t)idat_data_size);
    write_offset += 4U;
    const size_t idat_type_offset = write_offset;
    memcpy(png_buffer + write_offset, "IDAT", 4U);
    write_offset += 4U;
    memcpy(png_buffer + write_offset, compressed_buffer, idat_data_size);
    write_offset += idat_data_size;
    const uint32_t idat_crc32 = (uint32_t)crc32(
        initial_crc,
        png_buffer + idat_type_offset,
        (uInt)idat_crc_length
    );
    write_uint32_big_endian(png_buffer, write_offset, idat_crc32);
    write_offset += 4U;

    write_uint32_big_endian(png_buffer, write_offset, 0U);
    write_offset += 4U;
    const size_t iend_type_offset = write_offset;
    memcpy(png_buffer + write_offset, "IEND", 4U);
    write_offset += 4U;
    const uint32_t iend_crc32 = (uint32_t)crc32(initial_crc, png_buffer + iend_type_offset, 4U);
    write_uint32_big_endian(png_buffer, write_offset, iend_crc32);
    write_offset += 4U;

    output_file = fopen(output_path, "wb");
    if (output_file == nullptr) {
        fprintf(stderr, "Failed to open output file: %s\n", strerror(errno));
        goto cleanup;
    }

    if (fwrite(png_buffer, 1U, write_offset, output_file) != write_offset) {
        fprintf(stderr, "Failed to write PNG output\n");
        goto cleanup;
    }

    if (fclose(output_file) != 0) {
        output_file = nullptr;
        fprintf(stderr, "Failed to close PNG output\n");
        goto cleanup;
    }
    output_file = nullptr;
    succeeded = true;

cleanup:
    if (output_file != nullptr) {
        fclose(output_file);
    }
    free(png_buffer);
    free(compressed_buffer);
    free(raw_scanlines);
    return succeeded;
}

int main(int argument_count, char **argument_values) {
    int exit_code = EXIT_FAILURE;
    FILE *input_file = nullptr;
    uint8_t *file_buffer = nullptr;
    uint8_t *rgb_pixels = nullptr;
    StitchCanvas canvas = {};

    if (argument_count < 3 || argument_count > 4) {
        fprintf(
            stderr,
            "Usage: %s <input.{jef,jef+,pes,pec,dst}> <output.png> [max_dimension]\n",
            argument_values[0]
        );
        goto cleanup;
    }

    const char *input_path = argument_values[1];
    const char *output_path = argument_values[2];
    uint32_t max_dimension = DEFAULT_OUTPUT_DIMENSION;

    if (argument_count == 4) {
        char *parse_end = nullptr;
        errno = 0;
        const long parsed_dimension = strtol(argument_values[3], &parse_end, 10);
        if (errno != 0 || parse_end == argument_values[3] || *parse_end != '\0' ||
            parsed_dimension < (long)MIN_OUTPUT_DIMENSION ||
            parsed_dimension > (long)MAX_OUTPUT_DIMENSION) {
            fprintf(stderr, "Invalid max dimension: expected 64..4096\n");
            goto cleanup;
        }
        max_dimension = (uint32_t)parsed_dimension;
    }

    input_file = fopen(input_path, "rb");
    if (input_file == nullptr) {
        fprintf(stderr, "Failed to open input file: %s\n", strerror(errno));
        goto cleanup;
    }

    if (fseek(input_file, 0L, SEEK_END) != 0) {
        fprintf(stderr, "Failed to seek input file\n");
        goto cleanup;
    }
    const long file_length_long = ftell(input_file);
    if (file_length_long <= 0L || (uint64_t)file_length_long > (uint64_t)MAX_INPUT_FILE_SIZE) {
        fprintf(stderr, "Invalid input file size\n");
        goto cleanup;
    }
    if (fseek(input_file, 0L, SEEK_SET) != 0) {
        fprintf(stderr, "Failed to rewind input file\n");
        goto cleanup;
    }

    const size_t file_size = (size_t)file_length_long;
    file_buffer = (uint8_t *)malloc(file_size);
    if (file_buffer == nullptr) {
        fprintf(stderr, "Failed to allocate input buffer\n");
        goto cleanup;
    }

    const size_t bytes_read = fread(file_buffer, 1U, file_size, input_file);
    if (bytes_read != file_size) {
        fprintf(stderr, "Failed to read input file\n");
        goto cleanup;
    }
    fclose(input_file);
    input_file = nullptr;

    const EmbroideryFormat format = detect_embroidery_format(input_path, file_buffer, file_size);
    bool parse_succeeded = false;
    if (format == FORMAT_JEF) {
        parse_succeeded = parse_jef_file(file_buffer, file_size, &canvas);
    } else if (format == FORMAT_PES) {
        parse_succeeded = parse_pes_file(file_buffer, file_size, &canvas);
    } else if (format == FORMAT_PEC) {
        parse_succeeded = parse_pec_file(file_buffer, file_size, &canvas);
    } else if (format == FORMAT_DST) {
        parse_succeeded = parse_dst_file(file_buffer, file_size, &canvas);
    } else {
        fprintf(stderr, "Unsupported embroidery format\n");
        goto cleanup;
    }

    if (!parse_succeeded) {
        goto cleanup;
    }
    if (!canvas.has_bounds || canvas.segment_count == 0U) {
        fprintf(stderr, "No drawable stitches found\n");
        goto cleanup;
    }

    int64_t design_width = canvas.maximum_x - canvas.minimum_x;
    int64_t design_height = canvas.maximum_y - canvas.minimum_y;
    if (design_width < 1) {
        design_width = 1;
    }
    if (design_height < 1) {
        design_height = 1;
    }

    const double usable_dimension = (double)(max_dimension - CANVAS_PADDING_PIXELS * 2U);
    const double scale_x = usable_dimension / (double)design_width;
    const double scale_y = usable_dimension / (double)design_height;
    double scale = scale_x;
    if (scale_y < scale) {
        scale = scale_y;
    }

    uint32_t image_width =
        (uint32_t)((double)design_width * scale + 0.5) + CANVAS_PADDING_PIXELS * 2U;
    uint32_t image_height =
        (uint32_t)((double)design_height * scale + 0.5) + CANVAS_PADDING_PIXELS * 2U;
    if (image_width < 1U) {
        image_width = 1U;
    }
    if (image_height < 1U) {
        image_height = 1U;
    }
    if (image_width > max_dimension) {
        image_width = max_dimension;
    }
    if (image_height > max_dimension) {
        image_height = max_dimension;
    }

    size_t row_rgb_bytes = 0U;
    size_t pixel_buffer_size = 0U;
    if (ckd_mul(&row_rgb_bytes, (size_t)image_width, 3U) ||
        ckd_mul(&pixel_buffer_size, row_rgb_bytes, (size_t)image_height)) {
        fprintf(stderr, "Pixel buffer size overflow\n");
        goto cleanup;
    }

    rgb_pixels = (uint8_t *)malloc(pixel_buffer_size);
    if (rgb_pixels == nullptr) {
        fprintf(stderr, "Failed to allocate pixel buffer\n");
        goto cleanup;
    }

    /* Neutral gray background keeps both white and black threads visible. */
    memset(rgb_pixels, 0xD0, pixel_buffer_size);

    for (size_t segment_index = 0U; segment_index < canvas.segment_count; segment_index += 1U) {
        const StitchSegment segment = canvas.segments[segment_index];
        const int32_t raw_start_x =
            (int32_t)((double)((int64_t)segment.start_x - canvas.minimum_x) * scale + 0.5) +
            (int32_t)CANVAS_PADDING_PIXELS;
        const int32_t raw_start_y =
            (int32_t)((double)((int64_t)segment.start_y - canvas.minimum_y) * scale + 0.5) +
            (int32_t)CANVAS_PADDING_PIXELS;
        const int32_t raw_end_x =
            (int32_t)((double)((int64_t)segment.end_x - canvas.minimum_x) * scale + 0.5) +
            (int32_t)CANVAS_PADDING_PIXELS;
        const int32_t raw_end_y =
            (int32_t)((double)((int64_t)segment.end_y - canvas.minimum_y) * scale + 0.5) +
            (int32_t)CANVAS_PADDING_PIXELS;

        draw_line(
            rgb_pixels,
            image_width,
            image_height,
            clamp_pixel_coordinate(raw_start_x, image_width),
            clamp_pixel_coordinate(raw_start_y, image_height),
            clamp_pixel_coordinate(raw_end_x, image_width),
            clamp_pixel_coordinate(raw_end_y, image_height),
            segment.color
        );
    }

    if (!write_png_file(output_path, rgb_pixels, image_width, image_height)) {
        goto cleanup;
    }

    exit_code = EXIT_SUCCESS;

cleanup:
    if (input_file != nullptr) {
        fclose(input_file);
    }
    free(rgb_pixels);
    free(canvas.segments);
    free(file_buffer);
    return exit_code;
}
