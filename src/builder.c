#include "trilogy/builder.h"
#include "trilogy/error.h"
#include "trilogy/packet_parser.h"

#include <stdlib.h>
#include <string.h>

static int write_header(trilogy_builder_t *builder)
{
    int rc = trilogy_buffer_expand(builder->buffer, 4);

    if (rc < 0) {
        return rc;
    }

    builder->header_offset = builder->buffer->len;
    builder->fragment_length = 0;

    builder->buffer->buff[builder->buffer->len++] = 0;
    builder->buffer->buff[builder->buffer->len++] = 0;
    builder->buffer->buff[builder->buffer->len++] = 0;
    builder->buffer->buff[builder->buffer->len++] = builder->seq++;

    return TRILOGY_OK;
}

static int write_continuation_header(trilogy_builder_t *builder)
{
    builder->buffer->buff[builder->header_offset] = 0xff;
    builder->buffer->buff[builder->header_offset + 1] = 0xff;
    builder->buffer->buff[builder->header_offset + 2] = 0xff;

    return write_header(builder);
}

int trilogy_builder_init(trilogy_builder_t *builder, trilogy_buffer_t *buff, uint8_t seq)
{
    builder->buffer = buff;
    builder->buffer->len = 0;

    builder->seq = seq;

    return write_header(builder);
}

void trilogy_builder_finalize(trilogy_builder_t *builder)
{
    builder->buffer->buff[builder->header_offset + 0] = (builder->fragment_length >> 0) & 0xff;
    builder->buffer->buff[builder->header_offset + 1] = (builder->fragment_length >> 8) & 0xff;
    builder->buffer->buff[builder->header_offset + 2] = (builder->fragment_length >> 16) & 0xff;
}

#define CHECKED(expr)                                                                                                  \
    {                                                                                                                  \
        int rc = (expr);                                                                                               \
        if (rc) {                                                                                                      \
            return rc;                                                                                                 \
        }                                                                                                              \
    }

int trilogy_builder_write_uint8(trilogy_builder_t *builder, uint8_t val)
{
    CHECKED(trilogy_buffer_expand(builder->buffer, 1));

    builder->buffer->buff[builder->buffer->len++] = val;
    builder->fragment_length++;

    if (builder->fragment_length == TRILOGY_MAX_PACKET_LEN) {
        CHECKED(write_continuation_header(builder));
    }

    return TRILOGY_OK;
}

int trilogy_builder_write_uint16(trilogy_builder_t *builder, uint16_t val)
{
    CHECKED(trilogy_builder_write_uint8(builder, val & 0xff));
    CHECKED(trilogy_builder_write_uint8(builder, (val >> 8) & 0xff));

    return TRILOGY_OK;
}

int trilogy_builder_write_uint24(trilogy_builder_t *builder, uint32_t val)
{
    CHECKED(trilogy_builder_write_uint8(builder, val & 0xff));
    CHECKED(trilogy_builder_write_uint8(builder, (val >> 8) & 0xff));
    CHECKED(trilogy_builder_write_uint8(builder, (val >> 16) & 0xff));

    return TRILOGY_OK;
}

int trilogy_builder_write_uint32(trilogy_builder_t *builder, uint32_t val)
{
    CHECKED(trilogy_builder_write_uint8(builder, val & 0xff));
    CHECKED(trilogy_builder_write_uint8(builder, (val >> 8) & 0xff));
    CHECKED(trilogy_builder_write_uint8(builder, (val >> 16) & 0xff));
    CHECKED(trilogy_builder_write_uint8(builder, (val >> 24) & 0xff));

    return TRILOGY_OK;
}

int trilogy_builder_write_uint64(trilogy_builder_t *builder, uint64_t val)
{
    CHECKED(trilogy_builder_write_uint8(builder, val & 0xff));
    CHECKED(trilogy_builder_write_uint8(builder, (val >> 8) & 0xff));
    CHECKED(trilogy_builder_write_uint8(builder, (val >> 16) & 0xff));
    CHECKED(trilogy_builder_write_uint8(builder, (val >> 24) & 0xff));
    CHECKED(trilogy_builder_write_uint8(builder, (val >> 32) & 0xff));
    CHECKED(trilogy_builder_write_uint8(builder, (val >> 40) & 0xff));
    CHECKED(trilogy_builder_write_uint8(builder, (val >> 48) & 0xff));
    CHECKED(trilogy_builder_write_uint8(builder, (val >> 56) & 0xff));

    return TRILOGY_OK;
}

int trilogy_builder_write_lenenc(trilogy_builder_t *builder, uint64_t val)
{
    if (val < 251) {
        CHECKED(trilogy_builder_write_uint8(builder, (uint8_t)val));
    } else if (val <= 0xffff) {
        CHECKED(trilogy_builder_write_uint8(builder, 0xfc));
        CHECKED(trilogy_builder_write_uint16(builder, (uint16_t)val));
    } else if (val <= 0xffffff) {
        CHECKED(trilogy_builder_write_uint8(builder, 0xfd));
        CHECKED(trilogy_builder_write_uint24(builder, (uint32_t)val));
    } else { // val <= 0xffffffffffffffff
        CHECKED(trilogy_builder_write_uint8(builder, 0xfe));
        CHECKED(trilogy_builder_write_uint64(builder, val));
    }

    return TRILOGY_OK;
}

int trilogy_builder_write_buffer(trilogy_builder_t *builder, const void *data, size_t len)
{
    const char *ptr = data;

    size_t fragment_remaining = TRILOGY_MAX_PACKET_LEN - builder->fragment_length;

    // if this buffer write is not going to straddle a fragment boundary:
    if (len < fragment_remaining) {
        CHECKED(trilogy_buffer_expand(builder->buffer, len));

        memcpy(builder->buffer->buff + builder->buffer->len, ptr, len);

        builder->buffer->len += len;
        builder->fragment_length += len;

        return TRILOGY_OK;
    }

    // otherwise we're going to need to do this in multiple
    while (len >= fragment_remaining) {
        CHECKED(trilogy_buffer_expand(builder->buffer, fragment_remaining));

        memcpy(builder->buffer->buff + builder->buffer->len, ptr, fragment_remaining);

        builder->buffer->len += fragment_remaining;
        builder->fragment_length += fragment_remaining;

        ptr += fragment_remaining;
        len -= fragment_remaining;

        CHECKED(write_continuation_header(builder));
        fragment_remaining = TRILOGY_MAX_PACKET_LEN;
    }

    if (len) {
        CHECKED(trilogy_buffer_expand(builder->buffer, len));

        memcpy(builder->buffer->buff + builder->buffer->len, ptr, len);

        builder->buffer->len += len;
        builder->fragment_length += len;
    }

    return TRILOGY_OK;
}

int trilogy_builder_write_lenenc_buffer(trilogy_builder_t *builder, const void *data, size_t len)
{
    CHECKED(trilogy_builder_write_lenenc(builder, len));

    CHECKED(trilogy_builder_write_buffer(builder, data, len));

    return TRILOGY_OK;
}

int trilogy_builder_write_string(trilogy_builder_t *builder, const char *data)
{
    CHECKED(trilogy_builder_write_buffer(builder, (void *)data, strlen(data)));

    CHECKED(trilogy_builder_write_uint8(builder, 0));

    return TRILOGY_OK;
}

#undef CHECKED
