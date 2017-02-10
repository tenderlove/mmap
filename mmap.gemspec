# coding: utf-8
require File.expand_path('../lib/mmap/version', __FILE__)

Mmap::GEMSPEC = Gem::Specification.new do |spec|
  spec.name          = "mmap"
  spec.version       = Mmap::VERSION
  spec.authors       = ["Guy Decoux", "Aaron Patterson"]
  spec.license       = "Ruby"
  spec.email         = ["ts@moulon.inra.fr", "tenderlove@github.com"]
  spec.extensions    = ["ext/mmap/extconf.rb"]
  spec.homepage      = "https://github.com/tenderlove/mmap"
  spec.description   = %q{The Mmap class implement memory-mapped file objects}
  spec.summary       = %q{The Mmap class}

  spec.files         = `git ls-files Changes README.rdoc ext lib mmap.rd`.split
  spec.test_files    = `git ls-files b.rb test`.split
end
