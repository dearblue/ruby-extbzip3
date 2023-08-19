module Bzip3
  verreg = /^\s*[\-\*]\s+version:\s*(\d+(?:\.\w+)+)\s*$/i
  unless File.read("#{__dir__}/../../README.md", mode: "rt") =~ verreg
    raise "``version'' is not defined or bad syntax in ``README.md''"
  end

  VERSION = String($1)
end
