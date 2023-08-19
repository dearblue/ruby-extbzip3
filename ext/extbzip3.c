#include "extbzip3.h"

static uint32_t
aux_version_code(const char *ver)
{
    uint32_t code = 0;
    const char *o = ver, *p = o;
    int i = 4;

    while (i > 0) {
        if (*p == '\0' || *p == '.') {
            char *pp = (char *)p;
            unsigned long n = strtoul(o, &pp, 10);
            code = (code << 8) | (uint32_t)n;
            i--;

            if (*p == '\0' || i <= 1) {
                for (; i > 0; i--) {
                    code <<= 8;
                }

                break;
            }

            o = p + 1;
        }

        p++;
    }

    return code;
}

static VALUE
version_to_s(VALUE self)
{
    return rb_iv_get(self, "LIBBZIP3_VERSION");
}

static void
init_version(VALUE bzip3_module)
{
    RDOCFAKE(VALUE bzip3_module = rb_define_module("Bzip3"))

    VALUE libver = rb_ary_new_capa(3);
    const char *bz3ver = bz3_version();
    uint32_t code = aux_version_code(bz3ver);

    if (code < 0x01030200) {
        const char *e = getenv("RUBY_EXTBZIP3_USE_BZIP3_1_3_1_OR_OLDER");

        if (!e || strtol(e, NULL, 10) < 1) {
            rb_raise(rb_eLoadError, "%s",
                     "YOU ARE USING AN BZIP3-1.3.1 OR OLDER LIBRARY.\n"
                     "PLEASE UPDATE TO BZIP3-1.3.2 OR A NEWER LIBRARY ON YOUR SYSTEM.\n"
                     "\n"
                     "BZIP3-1.3.1 OR OLDER HAS DATA COMPATIBILITY ISSUES.\n"
                     "IF YOU WANT TO FORCE THE USE OF EXTBZIP3,\n"
                     "PLEASE SET THE FOLLOWING ENVIRONMENT VARIABLES:\n"
                     "\n"
                     "    RUBY_EXTBZIP3_USE_BZIP3_1_3_1_OR_OLDER=1");
        } else {
            rb_warn("%s",
                    "YOU ARE USING AN BZIP3-1.3.1 OR OLDER LIBRARY.\n"
                    "PLEASE UPDATE TO BZIP3-1.3.2 OR A NEWER LIBRARY ON YOUR SYSTEM.\n"
                    "\n"
                    "BZIP3-1.3.1 OR OLDER HAS DATA COMPATIBILITY ISSUES.\n"
                    "IF YOU CONTINUE USE, YOUR DATA WILL NOT BE ABLE TO BE MIGRATED IN THE FUTURE.");
        }
    }

    rb_ary_push(libver, INT2FIX((code >> 24) & 0xff));
    rb_ary_push(libver, INT2FIX((code >> 16) & 0xff));
    rb_ary_push(libver, INT2FIX((code >>  8) & 0xff));

    rb_iv_set(libver, "LIBBZIP3_VERSION", rb_str_new_static(bz3ver, strlen(bz3ver)));
    rb_iv_set(libver, "LIBBZIP3_VERSION_CODE", UINT2NUM(code));

    rb_define_singleton_method(libver, "to_s", version_to_s, 0);
    rb_obj_freeze(libver);

    rb_define_const(bzip3_module, "LIBRARY_VERSION", libver);
}

static void
init_constants(VALUE bzip3_module)
{
    RDOCFAKE(VALUE bzip3_module = rb_define_module("Bzip3"))

    rb_define_const(bzip3_module, "V1_FILE_FORMAT", INT2FIX(AUX_BZIP3_V1_FILE_FORMAT));
    rb_define_const(bzip3_module, "V1_FRAME_FORMAT", INT2FIX(AUX_BZIP3_V1_FRAME_FORMAT));
    rb_define_const(bzip3_module, "BLOCKSIZE_MIN", UINT2NUM(65 << 10));
    rb_define_const(bzip3_module, "BLOCKSIZE_MAX", UINT2NUM(511 << 20));
}

struct block_processor
{
    struct bz3_state *bzip3;
    size_t blocksize;
};

#define BLOCK_PROCESSOR_FREE_BLOCK(P)                                   \
        if ((P)->bzip3) {                                               \
            bz3_free((P)->bzip3);                                       \
        }                                                               \

#define BLOCK_PROCESSOR_VALUE_FOREACH(DEF)

AUX_DEFINE_TYPED_DATA(block_processor, block_processor_allocate, BLOCK_PROCESSOR_FREE_BLOCK, BLOCK_PROCESSOR_VALUE_FOREACH)

/*
 *  @overload initialize(blocksize)
 */
static VALUE
block_processor_initialize(VALUE self, VALUE blocksize)
{
    struct block_processor *p = get_block_processor_ptr(self);
    if (p->bzip3 != NULL) {
        rb_raise(rb_eTypeError, "wrong re-initializing - %" PRIsVALUE, self);
    }

    p->blocksize = NUM2UINT(blocksize);

    if (p->blocksize < AUX_BZIP3_BLOCKSIZE_MIN) {
        p->blocksize = AUX_BZIP3_BLOCKSIZE_MIN;
    } else if (p->blocksize > AUX_BZIP3_BLOCKSIZE_MAX) {
        rb_raise(rb_eRuntimeError, "blocksize too big - %" PRIsVALUE " (expect ..%u)",
                 blocksize, AUX_BZIP3_BLOCKSIZE_MAX);
    }

    p->bzip3 = aux_bz3_new((uint32_t)p->blocksize);

    return self;
}

static VALUE
block_processor_blocksize(VALUE self)
{
    return SIZET2NUM(get_block_processor(self)->blocksize);
}

static VALUE
block_processor_decode(VALUE self, VALUE src, VALUE dest, VALUE originalsize)
{
    rb_check_type(src, RUBY_T_STRING);
    rb_check_type(dest, RUBY_T_STRING);

    struct block_processor *p = get_block_processor(self);

    size_t origsize = NUM2SIZET(originalsize);
    if (origsize > (size_t)p->blocksize) {
        rb_raise(rb_eRuntimeError, "originalsize too big - %" PRIsVALUE, originalsize);
    }

    size_t srclen = RSTRING_LEN(src);
    size_t destcapa = bz3_bound((uint32_t)origsize);
    rb_str_modify(dest);
    rb_str_set_len(dest, 0);
    rb_str_modify_expand(dest, destcapa);

    memmove(RSTRING_PTR(dest), RSTRING_PTR(src), srclen);
    int32_t ret = aux_bz3_decode_block_nogvl(p->bzip3, RSTRING_PTR(dest), srclen, NUM2UINT(originalsize));
    extbzip3_check_error(ret);

    rb_str_set_len(dest, ret);

    return dest;
}

static VALUE
block_processor_encode(VALUE self, VALUE src, VALUE dest)
{
    rb_check_type(src, RUBY_T_STRING);
    rb_check_type(dest, RUBY_T_STRING);

    struct block_processor *p = get_block_processor(self);

    size_t srclen = RSTRING_LEN(src);
    if (srclen > p->blocksize) {
        rb_raise(rb_eRuntimeError, "src too big - #<%" PRIsVALUE ":0x%" PRIxVALUE ">", rb_class_of(src), src);
    }

    size_t destcapa = bz3_bound((uint32_t)srclen);
    rb_str_modify(dest);
    rb_str_set_len(dest, 0);
    rb_str_modify_expand(dest, (uint32_t)destcapa);

    memmove(RSTRING_PTR(dest), RSTRING_PTR(src), srclen);
    int32_t ret = aux_bz3_encode_block_nogvl(p->bzip3, RSTRING_PTR(dest), (int32_t)srclen);
    extbzip3_check_error(ret);

    rb_str_set_len(dest, ret);

    return dest;
}

static void
init_processor(VALUE bzip3_module)
{
    VALUE block_processor_class = rb_define_class_under(bzip3_module, "BlockProcessor", rb_cObject);
    rb_define_alloc_func(block_processor_class, block_processor_allocate);
    rb_define_method(block_processor_class, "initialize", block_processor_initialize, 1);
    rb_define_method(block_processor_class, "blocksize", block_processor_blocksize, 0);
    rb_define_method(block_processor_class, "decode", block_processor_decode, 3);
    rb_define_method(block_processor_class, "encode", block_processor_encode, 2);
}

EXTBZIP3_API void
Init_extbzip3(void)
{
    RB_EXT_RACTOR_SAFE(true);

    VALUE bzip3_module = rb_define_module("Bzip3");

    init_version(bzip3_module);
    init_constants(bzip3_module);
    init_processor(bzip3_module);
    extbzip3_init_decoder(bzip3_module);
    extbzip3_init_encoder(bzip3_module);
}
