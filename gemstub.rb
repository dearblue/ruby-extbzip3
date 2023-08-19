require_relative "lib/extbzip3/version"

GEMSTUB = Gem::Specification.new do |s|
  s.name = "extbzip3"
  s.version = Bzip3::VERSION
  s.summary = "ruby bindings for bzip3 (bz3)"
  s.description = <<~DESCRIPT
    unoficial ruby bindings for bzip3 (bz3) <https://github.com/kspalaiologos/bzip3>.
  DESCRIPT
  s.homepage = "https://github.com/dearblue/ruby-extbzip3"
  s.licenses = %w(BSD-2-Clause LGPL-3.0+ Apache-2.0)
  s.author = "dearblue"
  s.email = "dearblue@users.osdn.me"

  #s.required_ruby_version = ">= 2.0"
  s.add_development_dependency "rake", "~> 0"
end
