#!ruby

require "test-unit"
require "extbzip3"

SAMPLES = File.join(__dir__, "../sampledata")

class << SAMPLES
  def load_file(path)
    File.binread File.join(self, path)
  end

  def open_file(path, *args, **opts, &block)
    File.open File.join(self, path), *args, **opts, &block
  end
end

SAMPLES.freeze

class TestBzip3 < Test::Unit::TestCase
  def test_oneshot
    assert_equal "123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ\n",
                 Bzip3.decode(SAMPLES.load_file("single.bz3"))

    assert_raise RuntimeError do
      Bzip3.decode(SAMPLES.load_file("single.bz3-frame"))
    end

    assert_raise RuntimeError do
      Bzip3.decode(SAMPLES.load_file("single.bz3-frame"), format: Bzip3::V1_FILE_FORMAT)
    end

    assert_equal "123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ\nZYXWVUTSRQPONMLKJIHGFEDCBA987654321\n",
                 Bzip3.decode(SAMPLES.load_file("double.bz3"))

    assert_equal "123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ\n",
                 Bzip3.decode(SAMPLES.load_file("double.bz3"), concat: false)

    assert_raise RuntimeError do
      Bzip3.decode(SAMPLES.load_file("single+junks.bz3"))
    end
  end

  def test_oneshot_frame
    assert_equal "123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ\n",
                 Bzip3.decode(SAMPLES.load_file("single.bz3-frame"), format: Bzip3::V1_FRAME_FORMAT)

    assert_raise RuntimeError do
      Bzip3.decode(SAMPLES.load_file("single.bz3"), format: Bzip3::V1_FRAME_FORMAT)
    end
  end

  def test_stream
    io = SAMPLES.open_file("single.bz3")
    assert_kind_of Bzip3::Decoder, Bzip3.decode(io)
    assert_equal "123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ\n", Bzip3.decode(io).read
    io.rewind
    assert_equal "OK",
                 Bzip3.decode(io) { |bz3|
                   assert_equal "", bz3.read(0, "")
                   assert_equal "12345", bz3.read(5, "")
                   assert_equal "6789AB", bz3.read(6)
                   assert_equal "", bz3.read(0)
                   assert_equal "CDEFGHIJKLMNOPQRSTUVWXYZ\n", bz3.read
                   assert_equal "", bz3.read(0)
                   assert_equal nil, bz3.read
                   "OK"
                 }

    if defined? Ractor
      assert_equal "123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ\n", Ractor.new {
        io2 = SAMPLES.open_file("single.bz3")
        Bzip3.decode(io2).read
      }.take
    end
  end
end
