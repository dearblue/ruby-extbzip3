#include "extbzip3.h"

static inline int
aux_oneshot_encode(int format, uint32_t blocksize, const void *in, void *out, size_t insize, size_t *outsize)
{
    if (blocksize < AUX_BZIP3_BLOCKSIZE_MIN) {
        blocksize = AUX_BZIP3_BLOCKSIZE_MIN;
    } else if (blocksize > AUX_BZIP3_BLOCKSIZE_MAX) {
        return BZ3_ERR_INIT;
    }

    if (!outsize) {
        return BZ3_ERR_INIT;
    }

    int headersize;
    size_t blockcount;
    if (format == AUX_BZIP3_V1_FILE_FORMAT) {
        headersize = 9;
        blockcount = 0;
    } else {
        headersize = 13;
        blockcount = (insize / blocksize) + ((insize % blocksize != 0) ? 1 : 0);

        if (blockcount > (UINT32_MAX - 1)) {
            return BZ3_ERR_DATA_TOO_BIG;
        }
    }

    if (*outsize < (size_t)headersize) {
        return BZ3_ERR_INIT;
    }

    struct bz3_state *bz3 = bz3_new(blocksize);
    if (bz3 == NULL) {
        return BZ3_ERR_INIT;
    }

    const uint8_t *inp = (const uint8_t *)in;
    const uint8_t *const inend = inp + insize;
    uint8_t *outp = (uint8_t *)out + headersize;
    uint8_t *const outend = outp + *outsize;

    while (inend - inp > 0) {
        uint32_t origsize = ((inend - inp) > blocksize) ? blocksize : (uint32_t)(inend - inp);
        uint32_t packedsize = (uint32_t)bz3_bound(origsize); // TODO???: 過剰な値かも？

        if (outend - outp < packedsize) {
            bz3_free(bz3);
            return BZ3_ERR_DATA_TOO_BIG;
        }

        outp += 8;

        memmove(outp, inp, origsize);
        int32_t ret = aux_bz3_encode_block_nogvl(bz3, outp, origsize);
        if (ret < 0) {
            bz3_free(bz3);
            return ret;
        }

        storeu32le(outp - 8, ret);
        storeu32le(outp - 4, origsize);

        inp += origsize;
        outp += ret;
    }

    bz3_free(bz3);

    *outsize = (size_t)(outp - (const uint8_t *)out);

    memmove(out, aux_bzip3_signature, sizeof(aux_bzip3_signature));
    storeu32le((char *)out + 5, blocksize);

    if (format == AUX_BZIP3_V1_FRAME_FORMAT) {
        storeu32le((char *)out + 9, (uint32_t)blockcount);
    }

    return BZ3_OK;
}

struct encoder
{
    struct bz3_state *bzip3;
    uint32_t blocksize;
    int firstwrite:1;
    int closed:1;
    VALUE outport;
    VALUE srcbuf;
    VALUE destbuf;
};

#define ENCODER_FREE_BLOCK(P)                                           \
        if ((P)->bzip3) {                                               \
            bz3_free((P)->bzip3);                                       \
        }                                                               \

#define ENCODER_VALUE_FOREACH(DEF)                                      \
        DEF(outport)                                                    \
        DEF(srcbuf)                                                     \
        DEF(destbuf)                                                    \

AUX_DEFINE_TYPED_DATA(encoder, encoder_allocate, ENCODER_FREE_BLOCK, ENCODER_VALUE_FOREACH)

/*
 *  @overload initialize(blocksize: (16 << 20))
 */
static VALUE
encoder_initialize(int argc, VALUE argv[], VALUE self)
{
    struct { VALUE outport, opts; } args;
    rb_scan_args(argc, argv, "1:", &args.outport, &args.opts);

    enum { numkw = 1 };
    ID idtab[numkw] = { rb_intern("blocksize") };
    union { struct { VALUE blocksize; }; VALUE vect[numkw]; } opts;
    rb_get_kwargs(args.opts, idtab, 0, numkw, opts.vect);

    struct encoder *p = (struct encoder *)rb_check_typeddata(self, &encoder_type);
    if (p == NULL || p->bzip3) {
        rb_raise(rb_eTypeError, "wrong initialized or re-initializing - %" PRIsVALUE, self);
    }

    p->blocksize = aux_conv_to_blocksize(opts.blocksize);
    p->outport = args.outport;
    p->srcbuf = Qnil;
    p->destbuf = Qnil;
    p->bzip3 = aux_bz3_new(p->blocksize);
    p->firstwrite = 1;

    return self;
}

static VALUE
encoder_outport(VALUE self)
{
    return get_encoder(self)->outport;
}

static void
encoder_write_encode(VALUE self, struct encoder *p, const void *buf, size_t len)
{
    size_t bufoff = 8 + (p->firstwrite ? 9 : 0);

    p->destbuf = aux_str_new_recycle(p->destbuf, bufoff + bz3_bound(len));
    rb_str_set_len(p->destbuf, bufoff);
    rb_str_cat(p->destbuf, buf, len);

    int32_t res = aux_bz3_encode_block_nogvl(p->bzip3, RSTRING_PTR(p->destbuf) + bufoff, RSTRING_LEN(p->destbuf) - bufoff);

    if (res < 0) {
        extbzip3_check_error(res);
    }

    rb_str_set_len(p->destbuf, bufoff + res);
    storeu32le(RSTRING_PTR(p->destbuf) + bufoff - 8, res);
    storeu32le(RSTRING_PTR(p->destbuf) + bufoff - 4, (uint32_t)len);

    if (p->firstwrite) {
        memcpy(RSTRING_PTR(p->destbuf), "BZ3v1", 5);
        storeu32le(RSTRING_PTR(p->destbuf) + 5, p->blocksize);
        p->firstwrite = 0;
    }

    rb_funcallv(p->outport, rb_intern("<<"), 1, &p->destbuf);
}

static VALUE
encoder_write(VALUE self, VALUE src)
{
    struct encoder *p = get_encoder(self);

    rb_check_type(src, RUBY_T_STRING);

    size_t srclen = RSTRING_LEN(src);

    if (srclen <= p->blocksize) {
        if (rb_type_p(p->srcbuf, RUBY_T_STRING)) {
            size_t srcbuflen = RSTRING_LEN(p->srcbuf);
            size_t catlen = srclen + srcbuflen;

            if (catlen >= p->blocksize) {
                rb_str_cat(p->srcbuf, RSTRING_PTR(src), p->blocksize - srcbuflen);
                encoder_write_encode(self, p, RSTRING_PTR(p->srcbuf), RSTRING_LEN(p->srcbuf));
                rb_str_set_len(p->srcbuf, 0);
                rb_str_cat(p->srcbuf, RSTRING_PTR(src) + p->blocksize - srcbuflen, srclen - (p->blocksize - srcbuflen));

                return self;
            }
        } else {
            p->srcbuf = rb_str_new(0, 0);
        }

        rb_str_cat(p->srcbuf, RSTRING_PTR(src), srclen);

        return self;
    } else {
        size_t srcoff = 0;

        if (rb_type_p(p->srcbuf, RUBY_T_STRING) && RSTRING_LEN(p->srcbuf) > 0) {
            srcoff = p->blocksize - RSTRING_LEN(p->srcbuf);
            rb_str_cat(p->srcbuf, RSTRING_PTR(src), srcoff);
            encoder_write_encode(self, p, RSTRING_PTR(p->srcbuf), RSTRING_LEN(p->srcbuf));
            rb_str_set_len(p->srcbuf, 0);
            srclen = RSTRING_LEN(src); // maybe changed src with `outport << destbuf`
            if (srclen < srcoff) {
                return self;
            }
        }

        while (srclen - srcoff > p->blocksize) {
            encoder_write_encode(self, p, RSTRING_PTR(src) + srcoff, p->blocksize);
            srcoff += p->blocksize;
            srclen = RSTRING_LEN(src); // maybe changed src with `outport << destbuf`
            if (srclen < srcoff) {
                return self;
            }
        }

        if (srcoff < srclen) {
            if (!rb_type_p(p->srcbuf, RUBY_T_STRING)) {
                p->srcbuf = rb_str_new(0, 0);
            }

            rb_str_cat(p->srcbuf, RSTRING_PTR(src), srclen - srcoff);
        }

        return self;
    }
}

static VALUE
encoder_flush(VALUE self)
{
    struct encoder *p = get_encoder(self);

    if (rb_type_p(p->srcbuf, RUBY_T_STRING) && RSTRING_LEN(p->srcbuf) > 0) {
        encoder_write_encode(self, p, RSTRING_PTR(p->srcbuf), RSTRING_LEN(p->srcbuf));
        rb_str_set_len(p->srcbuf, 0);
    }

    return Qnil;
}

static VALUE
encoder_close(VALUE self)
{
    struct encoder *p = get_encoder(self);

    if (rb_type_p(p->srcbuf, RUBY_T_STRING) && RSTRING_LEN(p->srcbuf) > 0) {
        encoder_write_encode(self, p, RSTRING_PTR(p->srcbuf), RSTRING_LEN(p->srcbuf));
        rb_str_set_len(p->srcbuf, 0);
    }

    p->closed = 1;

    return Qnil;
}

static VALUE
encoder_closed_p(VALUE self)
{
    return get_encoder(self)->closed ? Qtrue : Qfalse;
}

/*
 *  @overload encode(src, maxdest = nil, dest = "", **opts)
 *  @overload encode(src, dest, **opts)
 *
 *  @return [String]
 *      dest as decompression data
 *  @param  [String]    src
 *  @param  [Integer]   maxdest
 *  @param  [String]    dest
 *  @option opts        [Integer]       :blocksize ((16 << 20))
 *  @option opts                        :format (Bzip3::V1_FILE_FORMAT)
 *      Bzip3::V1_FILE_FORMAT, Bzip3::V1_FRAME_FORMAT
 */
static VALUE
encoder_s_encode(int argc, VALUE argv[], VALUE mod)
{
    size_t insize, outsize;
    struct { VALUE src, maxdest, dest, opts; } args;
    switch (rb_scan_args(argc, argv, "12:", &args.src, &args.maxdest, &args.dest, &args.opts)) {
    case 1:
        insize = RSTRING_LEN(args.src);
        outsize = bz3_bound(insize);
        args.dest = rb_str_buf_new(outsize);
        break;
    case 2:
        insize = RSTRING_LEN(args.src);

        if (rb_type_p(args.maxdest, RUBY_T_FIXNUM) || rb_type_p(args.maxdest, RUBY_T_BIGNUM)) {
            outsize = NUM2SIZET(args.maxdest);
            args.dest = rb_str_buf_new(outsize);
        } else {
            args.dest = args.maxdest;
            outsize = bz3_bound(insize);
            rb_str_modify(args.dest);
            rb_str_set_len(args.dest, 0);
            rb_str_modify_expand(args.dest, outsize);
        }

        break;
    case 3:
        insize = RSTRING_LEN(args.src);
        outsize = NUM2SIZET(args.maxdest);
        rb_str_modify(args.dest);
        rb_str_set_len(args.dest, 0);
        rb_str_modify_expand(args.dest, outsize);

        break;
    }

    enum { numkw = 2 };
    ID idtab[numkw] = { rb_intern("blocksize"), rb_intern("format") };
    union { struct { VALUE blocksize, format; }; VALUE vect[numkw]; } opts;
    rb_get_kwargs(args.opts, idtab, 0, numkw, opts.vect);

    uint32_t blocksize = aux_conv_to_blocksize(opts.blocksize);
    int status = aux_oneshot_encode(aux_conv_to_format(opts.format), blocksize,
                                    RSTRING_PTR(args.src), RSTRING_PTR(args.dest),
                                    insize, &outsize);
    extbzip3_check_error(status);
    rb_str_set_len(args.dest, outsize);

    return args.dest;
}

void
extbzip3_init_encoder(VALUE bzip3_module)
{
    VALUE encoder_class = rb_define_class_under(bzip3_module, "Encoder", rb_cObject);
    rb_define_alloc_func(encoder_class, encoder_allocate);
    rb_define_singleton_method(encoder_class, "encode", encoder_s_encode, -1);
    rb_define_method(encoder_class, "initialize", encoder_initialize, -1);
    rb_define_method(encoder_class, "outport", encoder_outport, 0);
    rb_define_method(encoder_class, "write", encoder_write, 1);
    rb_define_method(encoder_class, "flush", encoder_flush, 0);
    rb_define_method(encoder_class, "close", encoder_close, 0);
    rb_define_method(encoder_class, "closed?", encoder_closed_p, 0);
    rb_define_alias(encoder_class, "<<", "write");
}
