#include "extbzip3.h"

static int32_t
aux_check_header(const char *in, const char *const inend, uint32_t *blockcount)
{
    if ((inend - in) < (blockcount ? 13 : 9)) {
        return BZ3_ERR_TRUNCATED_DATA;
    }

    if (memcmp(in, aux_bzip3_signature, 5) != 0) {
        return BZ3_ERR_MALFORMED_HEADER;
    }

    int32_t blocksize = loadu32le(in + 5);

    if (blocksize < AUX_BZIP3_BLOCKSIZE_MIN || blocksize > AUX_BZIP3_BLOCKSIZE_MAX) {
        return BZ3_ERR_MALFORMED_HEADER;
    }

    if (blockcount) {
        *blockcount = loadu32le(in + 9);

        if (*blockcount > INT32_MAX) {
            return BZ3_ERR_MALFORMED_HEADER;
        }
    }

    return blocksize;
}

static int
aux_oneshot_decode(const void *in, void *out, size_t insize, size_t *outsize, int format, int32_t blocksize, int concat)
{
    uint32_t blockcount = 0;
    int32_t ret = aux_check_header((const char *)in, (const char *)in + insize,
                                   (format == AUX_BZIP3_V1_FILE_FORMAT ? NULL : &blockcount));
    if (ret < 0) {
        return ret;
    }

    if (ret < AUX_BZIP3_BLOCKSIZE_MIN || ret > blocksize || ret > AUX_BZIP3_BLOCKSIZE_MAX) {
        return BZ3_ERR_OUT_OF_BOUNDS;
    }

    uint32_t chunk_blocksize = ret;
    struct bz3_state *bz3 = bz3_new(blocksize);

    if (bz3 == NULL) {
        return BZ3_ERR_INIT;
    }

    const char *inp = (const char *)in + (format == AUX_BZIP3_V1_FILE_FORMAT ? 9 : 13);
    const char *const inend = (const char *)in + insize;
    char *outp = (char *)out;
    char *const outend = outp + *outsize;

    while (inend - inp > 0) {
        if (blockcount == 0) {
            uint32_t blockcount1 = 0;
            ret = aux_check_header(inp, inend, (format == AUX_BZIP3_V1_FILE_FORMAT ? NULL : &blockcount1));

            if (ret > 0) {
                if (!concat) {
                    break;
                }

                blockcount = blockcount1;

                if (ret < AUX_BZIP3_BLOCKSIZE_MIN || ret > blocksize || ret > AUX_BZIP3_BLOCKSIZE_MAX) {
                    bz3_free(bz3);
                    return BZ3_ERR_OUT_OF_BOUNDS;
                }

                chunk_blocksize = (uint32_t)ret;
                inp += (format == AUX_BZIP3_V1_FILE_FORMAT ? 9 : 13);

                continue;
            }
        }

        if (inend - inp < 8) {
            bz3_free(bz3);
            return BZ3_ERR_TRUNCATED_DATA;
        }

        uint32_t packedsize = loadu32le(inp);
        uint32_t origsize = loadu32le(inp + 4);

        if (origsize > chunk_blocksize || packedsize > bz3_bound(origsize)) {
            bz3_free(bz3);
            return BZ3_ERR_DATA_TOO_BIG;
        }

        inp += 8;

        if (inend - inp < packedsize) {
            bz3_free(bz3);
            return BZ3_ERR_DATA_TOO_BIG;
        }

        if (outend - outp < (origsize > packedsize ? origsize : packedsize)) {
            bz3_free(bz3);
            return BZ3_ERR_DATA_TOO_BIG;
        }

        memmove(outp, inp, packedsize);
        ret = aux_bz3_decode_block_nogvl(bz3, outp, packedsize, origsize);
        if (ret < 0) {
            bz3_free(bz3);
            return ret;
        }

        inp += packedsize;
        outp += origsize;

        if (format != AUX_BZIP3_V1_FILE_FORMAT) {
            blockcount--;
        }
    }

    bz3_free(bz3);

    if (blockcount > 0) {
        return BZ3_ERR_TRUNCATED_DATA;
    }

    *outsize = (size_t)(outp - (const char *)out);

    return BZ3_OK;
}

static uint64_t
aux_scan_size(int format, const char *in, const char *const inend, int concat)
{
    return 16 << 20; // FIXME!
}

static int
aux_io_read(VALUE io, size_t size, VALUE buf)
{
    VALUE args[2] = { SIZET2NUM(size), buf };
    VALUE ret = rb_funcallv(io, rb_intern("read"), 2, args);

    if (RB_NIL_P(ret)) {
        return 1;
    } else if (ret != buf) {
        rb_check_type(ret, RUBY_T_STRING);
        rb_str_set_len(buf, 0);
        rb_str_cat(buf, RSTRING_PTR(ret), RSTRING_LEN(ret));
    }

    return 0;
}

struct decoder
{
    struct bz3_state *bzip3;
    uint32_t blocksize;
    int concat:1;
    int firstread:1;
    int closed:1;
    int eof:1;
    VALUE inport;
    VALUE readbuf;
    VALUE destbuf;
};

#define DECODER_FREE_BLOCK(P)                                           \
        if ((P)->bzip3) {                                               \
            bz3_free((P)->bzip3);                                       \
        }                                                               \

#define DECODER_VALUE_FOREACH(DEF)                                      \
        DEF(inport)                                                     \
        DEF(readbuf)                                                    \
        DEF(destbuf)                                                    \

AUX_DEFINE_TYPED_DATA(decoder, decoder_allocate, DECODER_FREE_BLOCK, DECODER_VALUE_FOREACH)

/*
 *  @overload initialize(blocksize: (16 << 20), concat: true)
 */
static VALUE
decoder_initialize(int argc, VALUE argv[], VALUE self)
{
    struct { VALUE inport, opts; } args;
    rb_scan_args(argc, argv, "1:", &args.inport, &args.opts);

    enum { numkw = 2 };
    ID idtab[numkw] = { rb_intern("blocksize"), rb_intern("concat") };
    union { struct { VALUE blocksize, concat; }; VALUE vect[numkw]; } opts;
    rb_get_kwargs(args.opts, idtab, 0, numkw, opts.vect);

    struct decoder *p = (struct decoder *)rb_check_typeddata(self, &decoder_type);
    if (p == NULL || p->bzip3) {
        rb_raise(rb_eTypeError, "wrong initialized or re-initializing - %" PRIsVALUE, self);
    }

    p->blocksize = aux_conv_to_blocksize(opts.blocksize);
    p->inport = args.inport;
    p->readbuf = Qnil;
    p->destbuf = Qnil;
    p->bzip3 = aux_bz3_new(p->blocksize);
    p->firstread = 1;
    p->concat = RB_UNDEF_P(opts.concat) || RTEST(opts.concat);

    return self;
}
static int
decoder_read_block(VALUE self, struct decoder *p)
{
    if (p->eof) {
        return 1;
    }

    if (p->firstread) {
        p->readbuf = rb_str_new(NULL, 0);

        if (aux_io_read(p->inport, 9, p->readbuf) != 0) {
            extbzip3_check_error(BZ3_ERR_MALFORMED_HEADER);
        }

        if (RSTRING_LEN(p->readbuf) < 9 || memcmp(RSTRING_PTR(p->readbuf), "BZ3v1", 5) != 0) {
            extbzip3_check_error(BZ3_ERR_MALFORMED_HEADER);
        }

        uint32_t blocksize = loadu32le(RSTRING_PTR(p->readbuf) + 5);
        if (blocksize < AUX_BZIP3_BLOCKSIZE_MIN || blocksize > AUX_BZIP3_BLOCKSIZE_MAX) {
            extbzip3_check_error(BZ3_ERR_MALFORMED_HEADER);
        }

        if (blocksize > p->blocksize) {
            rb_raise(rb_eRuntimeError, "initialize で指定した blocksize が小さすぎます (期待値 %d に対して実際は %d)", (int)p->blocksize, (int)blocksize);
        }

        p->firstread = 0;
    }

    for (;;) {
        if (aux_io_read(p->inport, 8, p->readbuf) != 0) {
            p->eof = 1;
            return 1;
        }

        if (RSTRING_LEN(p->readbuf) < 8) {
            extbzip3_check_error(BZ3_ERR_MALFORMED_HEADER);
        }

        if (memcmp(RSTRING_PTR(p->readbuf), "BZ3v1", 5) == 0) {
            char workbuf[4];
            memcpy(workbuf, RSTRING_PTR(p->readbuf) + 5, 3);
            if (aux_io_read(p->inport, 1, p->readbuf) == 0) {
                workbuf[3] = RSTRING_PTR(p->readbuf)[0];

                uint32_t blocksize = loadu32le(workbuf);
                if (blocksize < AUX_BZIP3_BLOCKSIZE_MIN || blocksize > AUX_BZIP3_BLOCKSIZE_MAX) {
                    extbzip3_check_error(BZ3_ERR_MALFORMED_HEADER);
                }

                if (blocksize > p->blocksize) {
                    rb_raise(rb_eRuntimeError, "initialize で指定した blocksize が小さすぎます (期待値 %d に対して実際は %d)", (int)p->blocksize, (int)blocksize);
                }

                if (p->concat) {
                    continue;
                } else {
                    p->eof = 1;
                    break;
                }
            }

            extbzip3_check_error(BZ3_ERR_MALFORMED_HEADER);
        }

        uint32_t packedsize = loadu32le(RSTRING_PTR(p->readbuf) + 0);
        uint32_t originsize = loadu32le(RSTRING_PTR(p->readbuf) + 4);

        rb_str_set_len(p->destbuf, 0);
        rb_str_modify_expand(p->destbuf, originsize);

        uint32_t needsize = packedsize;
        while (needsize > 0) {
            if (aux_io_read(p->inport, packedsize, p->readbuf) != 0) {
                rb_raise(rb_eRuntimeError, "意図しない EOF");
            } else if (RSTRING_LEN(p->readbuf) > packedsize) {
                rb_raise(rb_eRuntimeError, "#<%" PRIsVALUE ":0x%" PRIxVALUE ">#read は %u バイトを超過して読み込みました",
                         rb_class_of(p->inport), p->inport, packedsize);
            }

            rb_str_cat(p->destbuf, RSTRING_PTR(p->readbuf), RSTRING_LEN(p->readbuf));
            needsize -= RSTRING_LEN(p->readbuf);
        }

        int32_t ret = aux_bz3_decode_block_nogvl(p->bzip3, RSTRING_PTR(p->destbuf), packedsize, originsize);
        extbzip3_check_error(ret);

        rb_str_set_len(p->destbuf, originsize);

        break;
    }

    return 0;
}

/*
 *  @overload read(size = nil, dest = "")
 */
static VALUE
decoder_read(int argc, VALUE argv[], VALUE self)
{
    struct { VALUE size, dest; } args;
    switch (rb_scan_args(argc, argv, "02", &args.size, &args.dest)) {
    case 0:
        args.size = Qnil;
        args.dest = rb_str_new(NULL, 0);
        break;
    case 1:
        args.dest = rb_str_new(NULL, 0);
        break;
    case 2:
        rb_check_type(args.dest, RUBY_T_STRING);
        rb_str_modify(args.dest);
        rb_str_set_len(args.dest, 0);
        break;
    }

    size_t size;
    if (RB_NIL_P(args.size)) {
        size = -1;
    } else {
        size = NUM2SIZET(args.size);
    }

    struct decoder *p = get_decoder(self);

    if (p->closed) {
        rb_raise(rb_eRuntimeError, "closed stream - %" PRIsVALUE, self);
    }

    if (size < 1) {
        return args.dest;
    } else {
        RUBY_ASSERT_ALWAYS(rb_type_p(args.dest, RUBY_T_STRING) && RSTRING_LEN(args.dest) == 0);
        RUBY_ASSERT_ALWAYS(RB_NIL_P(p->destbuf) || rb_type_p(p->destbuf, RUBY_T_STRING));

        if (RB_NIL_P(p->destbuf)) {
            p->destbuf = rb_str_new(NULL, 0);
        }

        for (;;) {
            if ((size_t)RSTRING_LEN(p->destbuf) >= size) {
                rb_str_cat(args.dest, RSTRING_PTR(p->destbuf), size);
                memmove(RSTRING_PTR(p->destbuf), RSTRING_PTR(p->destbuf) + size, RSTRING_LEN(p->destbuf) - size);
                rb_str_set_len(p->destbuf, RSTRING_LEN(p->destbuf) - size);

                break;
            }

            size -= RSTRING_LEN(p->destbuf);
            rb_str_cat(args.dest, RSTRING_PTR(p->destbuf), RSTRING_LEN(p->destbuf));
            rb_str_set_len(p->destbuf, 0);

            if (decoder_read_block(self, p) != 0) {
                break;
            }
        }

        return (RSTRING_LEN(args.dest) > 0 ? args.dest : Qnil);
    }
}

static VALUE
decoder_close(VALUE self)
{
    struct decoder *p = get_decoder(self);

    if (p->closed) {
        rb_raise(rb_eRuntimeError, "closed stream - %" PRIsVALUE, self);
    }

    p->closed = 1;

    return Qnil;
}

static VALUE
decoder_closed(VALUE self)
{
    return (get_decoder(self)->closed ? Qtrue : Qfalse);
}

static VALUE
decoder_eof(VALUE self)
{
    return (get_decoder(self)->eof ? Qtrue : Qfalse);
}

/*
 *  @overload decode(src, maxdest = nil, dest = "", **opts)
 *  @overload decode(src, dest, **opts)
 *
 *  decode bzip3 sequence.
 *
 *  @param  src         [String]        describe bzip3 sequence
 *  @param  maxdest     [Integer]       describe maximum dest size
 *  @param  dest        [String]        describe destination
 *  @param  opts        [Hash]
 *  @option opts        [true, false]   :concat (true)
 *  @option opts        [true, false]   :partial (false)
 *  @option opts        [Integer]       :blocksize ((16 << 20))
 *      最大ブロックサイズを記述します。
 *  @option opts                        :format (Bzip3::V1_FILE_FORMAT)
 *      Bzip3::V1_FILE_FORMAT, Bzip3::V1_FRAME_FORMAT
 *  @return [String]    dest for decoded bzip3
 */
static VALUE
decoder_s_decode(int argc, VALUE argv[], VALUE mod)
{
    size_t insize, outsize;
    struct { VALUE src, maxdest, dest, opts; } args;
    argc = rb_scan_args(argc, argv, "12:", &args.src, &args.maxdest, &args.dest, &args.opts);

    enum { numkw = 4 };
    ID idtab[numkw] = { rb_intern("concat"), rb_intern("partial"), rb_intern("blocksize"), rb_intern("format") };
    union { struct { VALUE concat, partial, blocksize, format; }; VALUE vect[numkw]; } opts;
    rb_get_kwargs(args.opts, idtab, 0, numkw, opts.vect);

    switch (argc) {
    case 1:
        insize = RSTRING_LEN(args.src);
        outsize = aux_scan_size(aux_conv_to_format(opts.format), RSTRING_PTR(args.src), RSTRING_END(args.src), RB_UNDEF_P(opts.concat) || RTEST(opts.concat));
        args.dest = rb_str_buf_new(outsize);
        break;
    case 2:
        insize = RSTRING_LEN(args.src);

        if (rb_type_p(args.maxdest, RUBY_T_FIXNUM) || rb_type_p(args.maxdest, RUBY_T_BIGNUM)) {
            outsize = NUM2SIZET(args.maxdest);
            args.dest = rb_str_buf_new(outsize);
        } else {
            args.dest = args.maxdest;
            outsize = aux_scan_size(aux_conv_to_format(opts.format), RSTRING_PTR(args.src), RSTRING_END(args.src), RB_UNDEF_P(opts.concat) || RTEST(opts.concat));
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

    // TODO: maxdest, partial

    int status = aux_oneshot_decode(RSTRING_PTR(args.src), RSTRING_PTR(args.dest), insize, &outsize,
                                    aux_conv_to_format(opts.format),
                                    (RB_NIL_OR_UNDEF_P(opts.blocksize) ? (16 << 20) : NUM2INT(opts.blocksize)),
                                    RB_UNDEF_P(opts.concat) || RTEST(opts.concat));
    extbzip3_check_error(status);

    rb_str_set_len(args.dest, outsize);

    return args.dest;
}

void
extbzip3_init_decoder(VALUE bzip3_module)
{
    VALUE decoder_class = rb_define_class_under(bzip3_module, "Decoder", rb_cObject);
    rb_define_alloc_func(decoder_class, decoder_allocate);
    rb_define_singleton_method(decoder_class, "decode", decoder_s_decode, -1);
    rb_define_method(decoder_class, "initialize", decoder_initialize, -1);
    rb_define_method(decoder_class, "read", decoder_read, -1);
    rb_define_method(decoder_class, "close", decoder_close, 0);
    rb_define_method(decoder_class, "closed?", decoder_closed, 0);
    rb_define_method(decoder_class, "eof?", decoder_eof, 0);
    rb_define_alias(decoder_class, "eof", "eof?");
}
