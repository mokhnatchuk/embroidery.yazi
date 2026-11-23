#define _POSIX_C_SOURCE 200809L

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

constexpr size_t JEF_HEADER_SIZE = 116U;
constexpr size_t PEC_HEADER_SIZE = 532U;
constexpr size_t DST_HEADER_SIZE = 512U;

static void write_uint32_le(uint8_t *destination, size_t offset, uint32_t value) {
    destination[offset] = (uint8_t)(value & 0xFFU);
    destination[offset + 1U] = (uint8_t)((value >> 8) & 0xFFU);
    destination[offset + 2U] = (uint8_t)((value >> 16) & 0xFFU);
    destination[offset + 3U] = (uint8_t)((value >> 24) & 0xFFU);
}

static uint32_t read_uint32_be(const uint8_t *source, size_t offset) {
    const uint32_t b0 = (uint32_t)source[offset] << 24;
    const uint32_t b1 = (uint32_t)source[offset + 1U] << 16;
    const uint32_t b2 = (uint32_t)source[offset + 2U] << 8;
    const uint32_t b3 = (uint32_t)source[offset + 3U];
    return b0 | b1 | b2 | b3;
}

static bool write_binary_file(const char *path, const uint8_t *bytes, size_t byte_count) {
    bool succeeded = false;
    FILE *output_file = fopen(path, "wb");
    if (output_file == nullptr) {
        goto cleanup;
    }
    if (byte_count > 0U && fwrite(bytes, 1U, byte_count, output_file) != byte_count) {
        goto cleanup;
    }
    if (fclose(output_file) != 0) {
        output_file = nullptr;
        goto cleanup;
    }
    output_file = nullptr;
    succeeded = true;

cleanup:
    if (output_file != nullptr) {
        fclose(output_file);
    }
    return succeeded;
}

static int run_preview_binary(
    const char *binary_path,
    const char *input_path,
    const char *output_path,
    const char *dimension_text
) {
    const pid_t child_pid = fork();
    if (child_pid < 0) {
        return -1;
    }
    if (child_pid == 0) {
        char *arguments[5] = {
            (char *)binary_path,
            (char *)input_path,
            (char *)output_path,
            (char *)dimension_text,
            nullptr
        };
        execv(binary_path, arguments);
        _exit(127);
    }

    int wait_status = 0;
    if (waitpid(child_pid, &wait_status, 0) < 0) {
        return -1;
    }
    if (!WIFEXITED(wait_status)) {
        return -1;
    }
    return WEXITSTATUS(wait_status);
}

static bool verify_png_file(const char *png_path, uint32_t max_dimension) {
    bool valid = false;
    FILE *input_file = fopen(png_path, "rb");
    uint8_t *buffer = nullptr;

    if (input_file == nullptr) {
        goto cleanup;
    }
    if (fseek(input_file, 0L, SEEK_END) != 0) {
        goto cleanup;
    }
    const long size_long = ftell(input_file);
    if (size_long < 64L || size_long > 16777216L) {
        goto cleanup;
    }
    if (fseek(input_file, 0L, SEEK_SET) != 0) {
        goto cleanup;
    }

    const size_t file_size = (size_t)size_long;
    buffer = (uint8_t *)malloc(file_size);
    if (buffer == nullptr) {
        goto cleanup;
    }
    if (fread(buffer, 1U, file_size, input_file) != file_size) {
        goto cleanup;
    }

    static const uint8_t expected_signature[8] = {
        0x89U, 0x50U, 0x4EU, 0x47U, 0x0DU, 0x0AU, 0x1AU, 0x0AU
    };
    if (memcmp(buffer, expected_signature, sizeof(expected_signature)) != 0) {
        goto cleanup;
    }
    if (memcmp(buffer + 12U, "IHDR", 4U) != 0) {
        goto cleanup;
    }

    const uint32_t image_width = read_uint32_be(buffer, 16U);
    const uint32_t image_height = read_uint32_be(buffer, 20U);
    if (image_width == 0U || image_height == 0U ||
        image_width > max_dimension || image_height > max_dimension) {
        goto cleanup;
    }
    if (memcmp(buffer + 37U, "IDAT", 4U) != 0) {
        goto cleanup;
    }
    if (memcmp(buffer + file_size - 8U, "IEND", 4U) != 0) {
        goto cleanup;
    }
    valid = true;

cleanup:
    free(buffer);
    if (input_file != nullptr) {
        fclose(input_file);
    }
    return valid;
}

static bool write_synthetic_jef_pair(const char *jef_path, const char *jef_plus_path) {
    uint8_t buffer[256] = {};
    const uint32_t color_count = 2U;
    const uint32_t stitch_offset = (uint32_t)(JEF_HEADER_SIZE + color_count * 8U);

    write_uint32_le(buffer, 0U, stitch_offset);
    write_uint32_le(buffer, 24U, color_count);
    write_uint32_le(buffer, JEF_HEADER_SIZE, 10U); /* Janome red */
    write_uint32_le(buffer, JEF_HEADER_SIZE + 4U, 12U); /* Janome blue */

    size_t cursor = (size_t)stitch_offset;
    static const uint8_t stitches[] = {
        0x00U, 0x00U,
        24U, 24U,
        0x00U, (uint8_t)(int8_t)(-24),
        (uint8_t)(int8_t)(-24), 24U,
        0x80U, 0x01U, 30U, 0x00U, /* color change + jump */
        0x00U, 0x00U,
        24U, 24U,
        0x00U, (uint8_t)(int8_t)(-24),
        (uint8_t)(int8_t)(-24), 24U,
        0x80U, 0x10U /* end of stitches */
    };
    memcpy(buffer + cursor, stitches, sizeof(stitches));
    cursor += sizeof(stitches);

    if (!write_binary_file(jef_path, buffer, cursor)) {
        return false;
    }

    /* Append trailing JEF+ metadata after 0x80 0x10 to verify clean stop. */
    memset(buffer + cursor, 0x77U, 48U);
    return write_binary_file(jef_plus_path, buffer, cursor + 48U);
}

static bool write_synthetic_pes_and_pec(const char *pes_path, const char *pec_path) {
    uint8_t pec_payload[PEC_HEADER_SIZE + 64U] = {};
    memcpy(pec_payload, "LA:SYNTHETIC    ", 16U);
    pec_payload[48] = 1U;
    pec_payload[49] = 5U;
    pec_payload[50] = 20U;

    static const uint8_t stitch_stream[] = {
        0x00U, 0x00U,
        20U, 20U,
        0x00U, (uint8_t)(128U - 20U),
        (uint8_t)(128U - 20U), 20U,
        0x90U, 0x20U, 0x80U, 0x00U, /* 2-byte jump */
        0xFEU, 0xB0U, 0x01U, /* color change */
        0x00U, 0x00U,
        20U, 20U,
        0x8FU, 0xECU, 0x80U, 0x14U, /* 2-byte signed stitch */
        0xFFU
    };
    memcpy(pec_payload + PEC_HEADER_SIZE, stitch_stream, sizeof(stitch_stream));
    const size_t pec_length = PEC_HEADER_SIZE + sizeof(stitch_stream);

    uint8_t pec_file[8U + sizeof(pec_payload)] = {};
    memcpy(pec_file, "#PEC0001", 8U);
    memcpy(pec_file + 8U, pec_payload, pec_length);
    if (!write_binary_file(pec_path, pec_file, 8U + pec_length)) {
        return false;
    }

    uint8_t pes_file[16U + sizeof(pec_payload)] = {};
    memcpy(pes_file, "#PES0060", 8U);
    write_uint32_le(pes_file, 8U, 16U);
    memcpy(pes_file + 16U, pec_payload, pec_length);
    return write_binary_file(pes_path, pes_file, 16U + pec_length);
}

static bool write_synthetic_dst(const char *dst_path) {
    uint8_t dst_file[DST_HEADER_SIZE + 32U] = {};
    memset(dst_file, ' ', DST_HEADER_SIZE);
    memcpy(dst_file, "LA:PREVIEW_DST  ", 16U);
    dst_file[DST_HEADER_SIZE - 1U] = 0x1AU;

    /* Valid Tajima 3-byte stitch, jump, color-change, and EOF records. */
    static const uint8_t records[] = {
        0x00U, 0x00U, 0x03U, /* (0, 0) stitch */
        0x00U, 0x24U, 0x03U, /* (+27, -27) stitch */
        0x00U, 0x10U, 0x03U, /* (0, +27) stitch */
        0x00U, 0x28U, 0x03U, /* (-27, -27) stitch */
        0x00U, 0x04U, 0x83U, /* (+27, 0) jump */
        0x00U, 0x00U, 0xC3U, /* color change */
        0x00U, 0x00U, 0x03U, /* (0, 0) stitch */
        0x00U, 0x24U, 0x03U, /* (+27, -27) stitch */
        0x00U, 0x00U, 0xF3U  /* EOF */
    };
    memcpy(dst_file + DST_HEADER_SIZE, records, sizeof(records));
    return write_binary_file(dst_path, dst_file, DST_HEADER_SIZE + sizeof(records));
}

int main(int argument_count, char **argument_values) {
    if (argument_count != 2) {
        fprintf(stderr, "Usage: %s <path-to-embroidery-preview-binary>\n", argument_values[0]);
        return EXIT_FAILURE;
    }

    const char *binary_path = argument_values[1];
    const char *jef_path = "/tmp/embroidery_preview_test.jef";
    const char *jef_plus_path = "/tmp/embroidery_preview_test.jef+";
    const char *pes_path = "/tmp/embroidery_preview_test.pes";
    const char *pec_path = "/tmp/embroidery_preview_test.pec";
    const char *dst_path = "/tmp/embroidery_preview_test.dst";
    const char *corrupt_path = "/tmp/embroidery_preview_test_corrupt.jef";
    const char *output_png_path = "/tmp/embroidery_preview_test_out.png";

    int exit_code = EXIT_FAILURE;

    if (!write_synthetic_jef_pair(jef_path, jef_plus_path) ||
        !write_synthetic_pes_and_pec(pes_path, pec_path) ||
        !write_synthetic_dst(dst_path)) {
        fprintf(stderr, "TEST FAILED: could not generate synthetic inputs\n");
        goto cleanup;
    }

    const char *valid_inputs[5] = {jef_path, jef_plus_path, pes_path, pec_path, dst_path};
    for (size_t input_index = 0U; input_index < 5U; input_index += 1U) {
        remove(output_png_path);
        const int status = run_preview_binary(binary_path, valid_inputs[input_index], output_png_path, "256");
        if (status != 0) {
            fprintf(stderr, "TEST FAILED: preview binary returned %d for %s\n", status, valid_inputs[input_index]);
            goto cleanup;
        }
        if (!verify_png_file(output_png_path, 256U)) {
            fprintf(stderr, "TEST FAILED: invalid PNG output for %s\n", valid_inputs[input_index]);
            goto cleanup;
        }
    }
    printf("[PASS] synthetic .jef, .jef+, .pes, .pec, .dst rendered valid PNGs under ASan/UBSan\n");

    /* Verify corrupt input and invalid max dimension fail cleanly with non-zero exit. */
    const uint8_t corrupt_bytes[16] = {0x74U, 0x00U, 0x00U, 0x00U};
    if (!write_binary_file(corrupt_path, corrupt_bytes, sizeof(corrupt_bytes))) {
        goto cleanup;
    }
    const int corrupt_status = run_preview_binary(binary_path, corrupt_path, output_png_path, "256");
    if (corrupt_status == 0) {
        fprintf(stderr, "TEST FAILED: corrupt JEF unexpectedly succeeded\n");
        goto cleanup;
    }
    const int bad_dim_status = run_preview_binary(binary_path, jef_path, output_png_path, "99999");
    if (bad_dim_status == 0) {
        fprintf(stderr, "TEST FAILED: invalid max_dimension unexpectedly succeeded\n");
        goto cleanup;
    }
    printf("[PASS] corrupt inputs and out-of-range dimensions rejected cleanly\n");

    printf("ALL EMBROIDERY.YAZI C23 TESTS PASSED\n");
    exit_code = EXIT_SUCCESS;

cleanup:
    remove(jef_path);
    remove(jef_plus_path);
    remove(pes_path);
    remove(pec_path);
    remove(dst_path);
    remove(corrupt_path);
    remove(output_png_path);
    return exit_code;
}
