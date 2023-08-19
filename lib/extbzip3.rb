ver = RUBY_VERSION.slice(/\d+\.\d+/)
soname = File.basename(__FILE__, ".rb") << ".so"
require File.join(ver, soname)

module Bzip3
  refine String do
    def to_bzip3(*args, **opts)
      Bzip3::Encoder.encode(self, *args, **opts)
    end

    def bunzip3(*args, **opts)
      Bzip3::Decoder.decode(self, *args, **opts)
    end
  end

  refine BasicObject do
    def to_bzip3(*args, **opts, &block)
      Bzip3::Encoder.open(self, *args, **opts, &block)
    end

    def bunzip3(*args, **opts, &block)
      Bzip3::Decoder.open(self, *args, **opts, &block)
    end
  end

  class << Bzip3
    using Bzip3

    def encode(src, *args, **opts, &block)
      src.to_bzip3(*args, **opts, &block)
    end

    def decode(src, *args, **opts, &block)
      src.bunzip3(*args, **opts, &block)
    end
  end

  class << Decoder
    def open(*args, **opts, &block)
      bz3 = new(*args, **opts)
      return bz3 unless block

      begin
        yield bz3
      ensure
        bz3.close unless bz3.closed?
      end
    end
  end

  class << Encoder
    def open(*args, **opts, &block)
      bz3 = new(*args, **opts)
      return bz3 unless block

      begin
        yield bz3
      ensure
        bz3.close unless bz3.closed?
      end
    end
  end

  Bzip3 = self
end
