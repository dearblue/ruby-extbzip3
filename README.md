extbzip3 - bzip3 for Ruby
=========================

This document is written in Japanese.

[bzip3](https://github.com/kspalaiologos/bzip3) の非公式 Ruby バインディングです。


できること
----------

  - `require "extbzip3"`
      - `Bzip3` module

        | method                   | annotation
        | -----                    | -----
        | `Bzip3.decode(str, ...)` | see `Bzip3::Decoder.decode`
        | `Bzip3.encode(str, ...)` | see `Bzip3::Encoder.encode`
        | `Bzip3.decode(obj, ...)` | see `Bzip3::Decoder.open`
        | `Bzip3.encode(obj, ...)` | see `Bzip3::Encoder.open`

      - `Bzip3::Decoder` class

        | method                                                        | annotation
        | -----                                                         | -----
        | `Bzip3::Decoder.decode(str, maxdest = nil, dest = "", *opts)` | returns dest with bzip3 decoded
        | `Bzip3::Decoder.decode(str, dest, *opts)`                     | returns dest with bzip3 decoded
        | `Bzip3::Decoder.open(obj, *opts)`                             | returns bzip3 decoder
        | `Bzip3::Decoder.open(obj, *opts) { \|decoder\| ... }`         | returns object from yield returned
        | `Bzip3::Decoder#read(size = nil, dest = "")`                  | returns dest with bzip3 decoded
        | `Bzip3::Decoder#close`                                        |
        | `Bzip3::Decoder#eof?`                                         |

      - `Bzip3::Encoder` class

        | method                                                        | annotation
        | -----                                                         | -----
        | `Bzip3::Encoder.encode(str, maxdest = nil, dest = "", *opts)` | returns dest with bzip3'ed sequence
        | `Bzip3::Encoder.encode(str, dest, *opts)`                     | returns dest with bzip3'ed sequence
        | `Bzip3::Encoder.open(obj, *opts)`                             | returns bzip3 encoder
        | `Bzip3::Encoder.open(obj, *opts) { \|encoder\| ... }`         | returns object from yield returned
        | `Bzip3::Encoder#write(src)`                                   | returns receiver
        | `Bzip3::Encoder#flush`                                        | returns receiver
        | `Bzip3::Encoder#close`                                        |
        | `Bzip3::Encoder#eof?`                                         |

      - `Bzip3::BlockProcessor` class

        | method                                                        | annotation
        | -----                                                         | -----
        | `Bzip3::BlockProcessor.new(blocksize)`                        |
        | `Bzip3::BlockProcessor#decode(src, dest, original_size)`      | returns `dest` string as original data
        | `Bzip3::BlockProcessor#encode(src, dest)`                     | returns `dest` string as bzip3'ed data
        | `Bzip3::BlockProcessor#blocksize`                             | returns `blocksize` integer with when `new`

      - `using Bzip3` (refinements)

        | method                   | annotation
        | -----                    | -----
        | `String#to_bzip3(...)`   | see `Bzip3::Encoder.encode`
        | `String#bunzip3(...)`    | see `Bzip3::Decoder.decode`
        | `Object#to_bzip3(...)`   | see `Bzip3::Encoder.open`
        | `Object#bunzip3(...)`    | see `Bzip3::Decoder.open`

### データ形式について

  - extbzip3 は [「bzip3 ファイル形式」](https://github.com/kspalaiologos/bzip3/blob/1.3.1/doc/file_format.md) を標準で扱います。
    利用者は特別な操作や指定を行う必要がありません。
  - extbzip3 は [「bzip3 ブロック形式」](https://github.com/kspalaiologos/bzip3/blob/1.3.1/doc/low_level_format.md) を扱うことも出来ます。
    `Bzip3::BlockProcessor` クラスを使ってください。
  - extbzip3 は [「bzip3 フレーム形式」](https://github.com/kspalaiologos/bzip3/blob/1.3.1/doc/high_level_format.md) を扱うことも出来ます。
    文字列を圧縮・伸長する特異メソッド `Bzip3::Encoder.encode` または `Bzip3::Decoder.decode` にキーワード引数として `format: Bzip3::V1_FRAME_FORMAT` を与えてください。
    ストリーム指向 API を「bzip3 フレーム形式」に対応させる予定はありません。


つかいかた
----------

### 導入

事前にシステム上へ [bzip3](https://github.com/kspalaiologos/bzip3) をインストールしてください。

```console
% sudo gem install extbzip3
```

### 単発圧縮・伸長

```ruby
require "extbzip3"

src = "123456789"
bin = Bzip3.encode(src)
# => "BZ3v1\x00\x00\x10\x00\x11\x00\x00\x00\t\x00\x00\x00'F\xE3\xEB\xFF\xFF\xFF\xFF123456789"
src1 = Bzip3.decode(bin)
# => "123456789"
```

### ストリーミング圧縮と伸長

```ruby
require "extbzip3"

# 圧縮処理してファイルとして出力
File.open("/boot/kernel/kernel", "rb") do |src|
  File.open("kernel.bz3", "wb") do |dest|
    Bzip3.encode(dest) do |bz3|
      buf = ""
      bz3.write buf while src.read(123456, buf)
    end
  end
end

# 伸張処理してファイルとして出力
File.open("kernel.bz3", "rb") do |src|
  File.open("kernel.1", "wb") do |dest|
    Bzip3.decode(src) do |bz3|
      buf = ""
      dest.write buf while bz3.read(123456, buf)
    end
  end
end

# ファイルの比較
system "md5 /boot/kernel/kernel kernel.1"
# => MD5 (/boot/kernel/kernel) = 71b8f6c6a29f4d647f45d9501d549cf3
# => MD5 (kernel.1) = 71b8f6c6a29f4d647f45d9501d549cf3
```

### bzip3 フレーム形式による単発圧縮・伸長

```ruby
require "extbzip3"

src = "123456789"
bin = Bzip3.encode(src, format: Bzip3::V1_FRAME_FORMAT)
# => "BZ3v1\x00\x00\x10\x00\x01\x00\x00\x00\x11\x00\x00\x00\t\x00\x00\x00'F\xE3\xEB\xFF\xFF\xFF\xFF123456789"
src1 = Bzip3.decode(bin, format: Bzip3::V1_FRAME_FORMAT)
# => "123456789"
```


しょげん
--------

  - Project page: <https://github.com/dearblue/ruby-extbzip3>
  - Licensing: [2-clause BSD License](LICENSE) by [dearblue](https://github.com/dearblue)
  - Version: 0.0.1
  - Project status: CONCEPT
  - Dependency gems: none
  - Dependency external C libraries:
      - [bzip3](https://github.com/kspalaiologos/bzip3)
        version 1.3.2 or later
        under [GNU Lesser General Public License version 3 or later](https://github.com/kspalaiologos/bzip3/blob/master/LICENSE)
        by [Kamila Szewczyk](https://github.com/kspalaiologos)
        and [Apache License, Version 2.0](https://github.com/kspalaiologos/bzip3/blob/master/libsais-LICENSE)
        by [Ilya Grebnov](https://github.com/IlyaGrebnov)
        and [Kamila Szewczyk](https://github.com/kspalaiologos)
  - Bundled external C libraries: none
