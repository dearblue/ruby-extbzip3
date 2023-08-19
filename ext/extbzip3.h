#ifndef EXTBZIP3_H
#define EXTBZIP3_H 1

#include <ruby.h>
#include <ruby/thread.h>
#include <ruby/version.h>
#include "extconf.h"
#include "compat.h"
#include <libbz3.h>
#include <ctype.h>
#include <limits.h>
#include <stdlib.h>

#define RDOCFAKE(...)

#if RUBY_API_VERSION_CODE >= 20700
# define AUX_DEFINE_TYPED_DATA_COMPACT(...) __VA_ARGS__
#else
# define AUX_DEFINE_TYPED_DATA_COMPACT(...)
#endif

#define AUX_DEFINE_TYPED_DATA_GC_MARK(FIELD) rb_gc_mark_movable(_data_ptr->FIELD);
#define AUX_DEFINE_TYPED_DATA_GC_MOVE(FIELD) _data_ptr->FIELD = rb_gc_location(_data_ptr->FIELD);

#define AUX_DEFINE_TYPED_DATA(PREFIX, ALLOC_NAME, FREE_BLOCK, GC_VALUE) \
        static void                                                     \
        PREFIX ## _free(void *ptr)                                      \
        {                                                               \
            if (ptr) {                                                  \
                struct PREFIX *_data_ptr = (struct PREFIX *)ptr;        \
                                                                        \
                do {                                                    \
                    FREE_BLOCK(_data_ptr)                               \
                } while (0);                                            \
                                                                        \
                xfree(_data_ptr);                                       \
            }                                                           \
        }                                                               \
                                                                        \
        static void                                                     \
        PREFIX ## _mark(void *ptr)                                      \
        {                                                               \
            struct PREFIX *_data_ptr = (struct PREFIX *)ptr;            \
            (void)_data_ptr;                                            \
                                                                        \
            GC_VALUE(AUX_DEFINE_TYPED_DATA_GC_MARK)                     \
        }                                                               \
                                                                        \
        AUX_DEFINE_TYPED_DATA_COMPACT(                                  \
            static void                                                 \
            PREFIX ## _compact(void *ptr)                               \
            {                                                           \
                struct PREFIX *_data_ptr = (struct PREFIX *)ptr;        \
                (void)_data_ptr;                                        \
                                                                        \
                GC_VALUE(AUX_DEFINE_TYPED_DATA_GC_MOVE)                 \
            }                                                           \
        )                                                               \
                                                                        \
        static const rb_data_type_t PREFIX ## _type = {                 \
            "extbzip3:" #PREFIX,                                        \
            {                                                           \
                PREFIX ## _mark,                                        \
                PREFIX ## _free,                                        \
                NULL, /* PREFIX ## _size, */                            \
                AUX_DEFINE_TYPED_DATA_COMPACT(PREFIX ## _compact)       \
            },                                                          \
            0, 0, RUBY_TYPED_FREE_IMMEDIATELY                           \
        };                                                              \
                                                                        \
        static VALUE                                                    \
        ALLOC_NAME(VALUE mod)                                           \
        {                                                               \
            return rb_data_typed_object_zalloc(mod, sizeof(struct PREFIX), &PREFIX ## _type); \
        }                                                               \
                                                                        \
        static struct PREFIX *                                          \
        get_ ## PREFIX ## _ptr(VALUE obj)                               \
        {                                                               \
            struct PREFIX *p = (struct PREFIX *)rb_check_typeddata(obj, &PREFIX ## _type); \
                                                                        \
            if (!p) {                                                   \
                rb_raise(rb_eArgError, "wrong allocated object - %" PRIsVALUE, obj); \
            }                                                           \
                                                                        \
            return p;                                                   \
        }                                                               \
                                                                        \
        static struct PREFIX *                                          \
        get_ ## PREFIX(VALUE obj)                                       \
        {                                                               \
            struct PREFIX *p = get_ ## PREFIX ## _ptr(obj);             \
                                                                        \
            if (!p->bzip3) {                                            \
                rb_raise(rb_eArgError, "wrong initialized - %" PRIsVALUE, obj); \
            }                                                           \
                                                                        \
            return p;                                                   \
        }                                                               \


void extbzip3_init_decoder(VALUE bzip3_module);
void extbzip3_init_encoder(VALUE bzip3_module);

static inline void
extbzip3_check_error(int status)
{
    if (status < BZ3_OK) {
        switch (status) {
        case BZ3_ERR_OUT_OF_BOUNDS:
            rb_raise(rb_eRuntimeError, "%s", "BZ3_ERR_OUT_OF_BOUNDS");
        case BZ3_ERR_BWT:
            rb_raise(rb_eRuntimeError, "%s", "BZ3_ERR_BWT");
        case BZ3_ERR_CRC:
            rb_raise(rb_eRuntimeError, "%s", "BZ3_ERR_CRC");
        case BZ3_ERR_MALFORMED_HEADER:
            rb_raise(rb_eRuntimeError, "%s", "BZ3_ERR_MALFORMED_HEADER");
        case BZ3_ERR_TRUNCATED_DATA:
            rb_raise(rb_eRuntimeError, "%s", "BZ3_ERR_TRUNCATED_DATA");
        case BZ3_ERR_DATA_TOO_BIG:
            rb_raise(rb_eRuntimeError, "%s", "BZ3_ERR_DATA_TOO_BIG");
        case BZ3_ERR_INIT:
            rb_raise(rb_eRuntimeError, "%s", "BZ3_ERR_INIT");
        default:
            rb_raise(rb_eRuntimeError, "unknown error (code: %d)", status);
        }
    }
}

#define AUX_BZIP3_BLOCKSIZE_MIN (65 << 10)
#define AUX_BZIP3_BLOCKSIZE_MAX (511 << 20)

#define AUX_BZIP3_V1_FILE_FORMAT  1
#define AUX_BZIP3_V1_FRAME_FORMAT 2

static const char aux_bzip3_signature[5] = { 'B', 'Z', '3', 'v', '1' };

static inline int
aux_conv_to_format(VALUE o)
{
    if (RB_NIL_OR_UNDEF_P(o)) {
        return AUX_BZIP3_V1_FILE_FORMAT;
    }

    int n = NUM2INT(o);

    switch (n) {
    case AUX_BZIP3_V1_FILE_FORMAT:
    case AUX_BZIP3_V1_FRAME_FORMAT:
        return n;
    default:
        rb_raise(rb_eRuntimeError, "wrong format type");
    }
}

static inline struct bz3_state *
aux_bz3_new(uint32_t blocksize)
{
    struct bz3_state *p = bz3_new(blocksize);

    if (!p) {
        rb_gc_start();
        p = bz3_new(blocksize);

        if (!p) {
            rb_raise(rb_eNoMemError, "probabry out of memory");
        }
    }

    return p;
}

static inline uint32_t
aux_conv_to_blocksize(VALUE obj)
{
    if (RB_NIL_OR_UNDEF_P(obj)) {
        return 16 << 20;
    } else {
        uint32_t blocksize = NUM2UINT(obj);

        if (blocksize < AUX_BZIP3_BLOCKSIZE_MIN) {
            return AUX_BZIP3_BLOCKSIZE_MIN;
        } else if (blocksize > AUX_BZIP3_BLOCKSIZE_MAX) {
            rb_raise(rb_eArgError, "out of range for blocksize (expect %d..%d, but given %d)",
                     AUX_BZIP3_BLOCKSIZE_MIN, AUX_BZIP3_BLOCKSIZE_MAX, (int)blocksize);
        }

        return blocksize;
    }
}

static inline VALUE
aux_str_new_recycle(VALUE str, size_t capa)
{
    if (RB_NIL_P(str)) {
        str = rb_str_new(NULL, 0);
    } else {
        rb_str_set_len(str, 0);
    }

    rb_str_modify_expand(str, capa);

    return str;
}

static inline uint32_t
loadu32le(const void *buf)
{
    const uint8_t *p = (const uint8_t *)buf;

    return ((uint32_t)p[0] <<  0) |
           ((uint32_t)p[1] <<  8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static inline void
storeu32le(void *buf, uint32_t n)
{
    uint8_t *p = (uint8_t *)buf;

    p[0] = (n >>  0) & 0xff;
    p[1] = (n >>  8) & 0xff;
    p[2] = (n >> 16) & 0xff;
    p[3] = (n >> 24) & 0xff;
}

struct aux_bz3_decode_block_nogvl_main
{
    struct bz3_state *bz3;
    uint8_t *buf;
    int32_t buflen;
    int32_t originsize;
};

static inline void *
aux_bz3_decode_block_nogvl_main(void *opaque)
{
    struct aux_bz3_decode_block_nogvl_main *p = (struct aux_bz3_decode_block_nogvl_main *)opaque;
    return (void *)(intptr_t)bz3_decode_block(p->bz3, p->buf, p->buflen, p->originsize);
}

static inline int32_t
aux_bz3_decode_block_nogvl(struct bz3_state *bz3, void *buf, size_t buflen, size_t originsize)
{
    if (buflen > INT32_MAX || originsize > INT32_MAX) {
        return BZ3_ERR_DATA_TOO_BIG;
    }

    struct aux_bz3_decode_block_nogvl_main args = { bz3, (uint8_t *)buf, (int32_t)buflen, (int32_t)originsize };
    return (int32_t)(intptr_t)rb_thread_call_without_gvl(aux_bz3_decode_block_nogvl_main, &args, NULL, NULL);
}

struct aux_bz3_encode_block_nogvl_main
{
    struct bz3_state *bz3;
    uint8_t *buf;
    int32_t buflen;
};

static inline void *
aux_bz3_encode_block_nogvl_main(void *opaque)
{
    struct aux_bz3_encode_block_nogvl_main *p = (struct aux_bz3_encode_block_nogvl_main *)opaque;
    return (void *)(intptr_t)bz3_encode_block(p->bz3, p->buf, p->buflen);
}

static inline int32_t
aux_bz3_encode_block_nogvl(struct bz3_state *bz3, void *buf, size_t buflen)
{
    if (buflen > INT32_MAX) {
        return BZ3_ERR_DATA_TOO_BIG;
    }

    struct aux_bz3_encode_block_nogvl_main args = { bz3, (uint8_t *)buf, (int32_t)buflen };
    return (int32_t)(intptr_t)rb_thread_call_without_gvl(aux_bz3_encode_block_nogvl_main, &args, NULL, NULL);
}

#endif // EXTBZIP3_H
