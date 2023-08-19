require "fiddle/import"

module Bzip3FFI
  module LIBBZIP3
    extend Fiddle::Importer

    dlload "libbzip3.so"

    extern "BZIP3_API const char *bz3_version(void)"
    extern "size_t bz3_bound(size_t input_size)"
    extern "int bz3_compress(uint32_t block_size, const uint8_t *in, uint8_t *out, size_t in_size, size_t *out_size)"
    extern "int bz3_decompress(const uint8_t *in, uint8_t *out, size_t in_size, size_t *out_size)"
    extern "struct bz3_state *bz3_new(int32_t block_size)"
    extern "void bz3_free(struct bz3_state *state)"
    extern "int8_t bz3_last_error(struct bz3_state *state)"
    extern "const char *bz3_strerror(struct bz3_state *state)"
    extern "int32_t bz3_encode_block(struct bz3_state *state, uint8_t *buffer, int32_t size)"
    extern "int32_t bz3_decode_block(struct bz3_state *state, uint8_t *buffer, int32_t size, int32_t orig_size)"

    def LIBBZIP3.strerror(code)
      case code
      when 0;   nil
      when -1;  "BZ3_ERR_OUT_OF_BOUNDS"
      when -2;  "BZ3_ERR_BWT"
      when -3;  "BZ3_ERR_CRC"
      when -4;  "BZ3_ERR_MALFORMED_HEADER"
      when -5;  "BZ3_ERR_TRUNCATED_DATA"
      when -6;  "BZ3_ERR_DATA_TOO_BIG"
      when -7;  "BZ3_ERR_INIT"
      else;     "unknown error (code: #{code})"
      end
    end
  end

  DEFAULT_BLOCKSIZE = 16 << 20
  ZERO_BLOCK = ("\0".b * (256 << 10)).freeze

  ver = LIBBZIP3.bz3_version
  LIBRARY_VERSION = ver.to_s.split(".").map { |e| e.to_i }

  class << LIBRARY_VERSION
    ver = LIBBZIP3.bz3_version.to_s.freeze
    define_method(:to_s, &-> { ver })
  end

  if (LIBRARY_VERSION <=> [1, 3, 2]) < 0
    if ENV["RUBY_EXTBZIP3_USE_BZIP3_1_3_1_OR_OLDER"].to_i < 1
      raise LoadError, <<~ERR.chomp
        YOU ARE USING AN BZIP3-1.3.1 OR OLDER LIBRARY.
        PLEASE UPDATE TO BZIP3-1.3.2 OR A NEWER LIBRARY ON YOUR SYSTEM.

        BZIP3-1.3.1 OR OLDER HAS DATA COMPATIBILITY ISSUES.
        IF YOU WANT TO FORCE THE USE OF EXTBZIP3,
        PLEASE SET THE FOLLOWING ENVIRONMENT VARIABLES:

            RUBY_EXTBZIP3_USE_BZIP3_1_3_1_OR_OLDER=1
      ERR
    else
      warn <<~WARN.chomp
        YOU ARE USING AN BZIP3-1.3.1 OR OLDER LIBRARY
        PLEASE UPDATE TO BZIP3-1.3.2 OR A NEWER LIBRARY ON YOUR SYSTEM.

        BZIP3-1.3.1 OR OLDER HAS DATA COMPATIBILITY ISSUES.
        IF YOU CONTINUE USE, YOUR DATA WILL NOT BE ABLE TO BE MIGRATED IN THE FUTURE.
      WARN
    end
  end

  using Module.new {
    refine String do
      def expand_buffer(capa)
        expands = capa - self.bytesize

        if expands > 0
          (expands / ZERO_BLOCK.bytesize).times { self << ZERO_BLOCK }
          self << ZERO_BLOCK.byteslice(0, expands % ZERO_BLOCK.bytesize)
        end

        self
      end

      def set_len(len)
        enc = encoding
        force_encoding(Encoding::BINARY)
        self[len..-1] = ""
        self
      ensure
        force_encoding(enc) rescue nil if enc
      end
    end
  }

  class Decoder
    class << self
      def decode(src, *args, blocksize: nil, concat: false, partial: false)
        case args.size
        when 0
          ;
        when 1
          if args[0].kind_of?(Integer)
            ;
          else
            args.unshift nil
          end
        when 2
          ;
        else
          raise ArgumentError, "wrong number of arguments (expect 1..3, given #{args.size + 1})"
        end

        maxdest = args[0] ? Integer(args[0].to_i) : LIBBZIP3.bz3_bound(src.bytesize)
        dest = args[1] ? args[1].expand_buffer(maxdest) : "\0".b * maxdest

        sizebuf = [maxdest].pack("J!")

        ret = LIBBZIP3.bz3_decompress(src, dest, src.bytesize, sizebuf)
        raise LIBBZIP3.strerror(ret) if ret != 0

        dest.set_len sizebuf.unpack1("J!")
      end
    end
  end

  class Encoder
    class << self
      def encode(src, *args, blocksize: DEFAULT_BLOCKSIZE)
        case args.size
        when 0
          ;
        when 1
          if args[0].kind_of?(Integer)
            ;
          else
            args.unshift nil
          end
        when 2
          ;
        else
          raise ArgumentError, "wrong number of arguments (expect 1..3, given #{args.size + 1})"
        end

        maxdest = args[0] ? Integer(args[0].to_i) : LIBBZIP3.bz3_bound(src.bytesize)
        dest = args[1] ? args[1].expand_buffer(maxdest) : "\0".b * maxdest

        sizebuf = [maxdest].pack("J!")

        ret = LIBBZIP3.bz3_compress(blocksize, src, dest, src.bytesize, sizebuf)
        raise LIBBZIP3.strerror(ret) if ret != 0

        dest.set_len sizebuf.unpack1("J!")
      end
    end
  end

  refine String do
    def to_bzip3(*args, **opts)
      Encoder.encode(self, *args, **opts)
    end

    def bunzip3(*args, **opts)
      Decoder.decode(self, *args, **opts)
    end
  end

  refine BasicObject do
    def to_bzip3(*args, **opts, &block)
      Encoder.open(self, *args, **opts, &block)
    end

    def bunzip3(*args, **opts, &block)
      Decoder.open(self, *args, **opts, &block)
    end
  end
end

if $0 == __FILE__
  bzip3 = ENV["BZIP3"] || "bzip3"
  dest = dest2 = nil

  IO.popen(%W(#{bzip3} -e -b 1), "r+b") do |io|
    io << "123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ\n"
    io.close_write
    dest = io.read
  end

  IO.popen(%W(#{bzip3} -e -b 1), "r+b") do |io|
    io << "ZYXWVUTSRQPONMLKJIHGFEDCBA987654321\n"
    io.close_write
    dest2 = io.read
  end

  File.binwrite(File.join(__dir__, "../sampledata/single.bz3"), dest)
  File.binwrite(File.join(__dir__, "../sampledata/double.bz3"), [dest, dest2].pack("a*a*"))
  File.binwrite(File.join(__dir__, "../sampledata/single+junks.bz3"), [dest, "<<<ゴミデータ>>>"].pack("a*a*"))

  str = "123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ\n"
  bz3seq = "\0".b * (64 << 10)
  seqsize = [bz3seq.bytesize].pack("J")

  if Bzip3FFI::LIBBZIP3.bz3_compress(1 << 20, str, bz3seq, str.bytesize, seqsize) == 0
    bz3seq[seqsize.unpack1("J") .. -1] = ""
  else
    raise "failed bz3_compress"
  end

  File.binwrite(File.join(__dir__, "../sampledata/single.bz3-frame"), bz3seq)
end
