# coding: utf-8
lib = File.expand_path('../lib', __FILE__)
$LOAD_PATH.unshift(lib) unless $LOAD_PATH.include?(lib)
require 'mmap'

Gem::Specification.new do |spec|
  spec.name          = "mmap"
  spec.version       = Mmap::VERSION
  spec.authors       = ["Guy Decoux", "Aaron Patterson"]
  spec.email         = ["ts@moulon.inra.fr", "tenderlove@github.com"]
  spec.description   = %q{The Mmap class implement memory-mapped file objects}
  spec.summary       = %q{The Mmap class implement memory-mapped file objects}
  spec.homepage      = "https://github.com/tenderlove/mmap"
  spec.license       = "https://www.ruby-lang.org/en/about/license.txt"

  spec.files         = `git ls-files`.split($/)
  spec.executables   = spec.files.grep(%r{^bin/}) { |f| File.basename(f) }
  spec.test_files    = spec.files.grep(%r{^(test|spec|features)/})
  spec.require_paths = ["lib"]

  spec.add_development_dependency "rake"
  spec.add_development_dependency "rake-compiler"
  spec.add_development_dependency "hoe"
end