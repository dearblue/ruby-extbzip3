#!ruby

require "mkmf"

using Module.new {
  refine Object do
    def has_function_modifier?(modifier_code)
      try_compile(<<~CODE)
        #{modifier_code}
        void
        func1(void) {
        }
      CODE
    end
  end
}

MakeMakefile::CONFIG["optflags"] = "-O0 -g3" if ENV["RUBY_EXTBZIP3_DEBUG"].to_i > 0

have_header("libbz3.h") or abort "need libbz3.h header file"
have_library("bzip3") or abort "need libbzip3 library"

if RbConfig::CONFIG["arch"] =~ /mingw/i
  #$LDFLAGS << " -static-libgcc" if try_ldflags("-static-libgcc")
else
  if try_compile(<<~"VISIBILITY")
      __attribute__((visibility("hidden"))) int conftest(void) { return 0; }
     VISIBILITY

    if try_cflags("-fvisibility=hidden")
      $CFLAGS << " -fvisibility=hidden"
      $defs << %(-DEXTBZIP3_API='__attribute__((visibility("default")))')
    end
  end
end

mod = %w(__attribute__((__noreturn__)) __declspec(noreturn) [[noreturn]] _Noreturn).find { |m|
  has_function_modifier?(m)
}
$defs << %(-DRBEXT_NORETURN='#{mod}')

create_header
create_makefile File.join(RUBY_VERSION.slice(/\d+\.\d+/), "extbzip3")
