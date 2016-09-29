# coding: utf-8
lib = File.expand_path('../lib', __FILE__)
$LOAD_PATH.unshift(lib) unless $LOAD_PATH.include?(lib)
require 'mmap/version'

Gem::Specification.new do |spec|
  spec.name          = "mmap"
  spec.version       = Mmap::VERSION
  spec.authors       = ["Guy Decoux", "Aaron Patterson"]
  spec.email         = ["ts@moulon.inra.fr", "tenderlove@github.com"]
  spec.summary       = %q{A Ruby interface to memory-mapped file objects}
  spec.description   = %q{Allows you to memory map large files, which increases I/O performance.}
  spec.homepage      = "https://github.com/tenderlove/mmap"
  spec.license       = "Ruby"

  spec.files         = `git ls-files`.split($/)
  spec.executables   = spec.files.grep(%r{^bin/}) { |f| File.basename(f) }
  spec.test_files    = spec.files.grep(%r{^(test|spec|features)/})
  spec.extensions    = ["ext/mmap/extconf.rb"]
  spec.require_paths = ["lib"]

  spec.add_development_dependency "rake"
  spec.add_development_dependency "rake-compiler"
  spec.add_development_dependency 'minitest'
end