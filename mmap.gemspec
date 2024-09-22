# frozen_string_literal: true

require_relative 'lib/mmap/version'

Gem::Specification.new do |spec|
  spec.name = 'mmap'
  spec.version = Mmap::VERSION
  spec.authors = ['Guy Decoux', 'Aaron Patterson']
  spec.email = ['ts@moulon.inra.fr', 'tenderlove@github.com']
  spec.description = 'The Mmap class implement memory-mapped file objects'
  spec.summary = 'The Mmap class implement memory-mapped file objects'
  spec.homepage = 'https://github.com/tenderlove/mmap'
  spec.required_ruby_version = '>= 2.7'
  spec.metadata['homepage_uri'] = spec.homepage

  # Specify which files should be added to the gem when it is released.
  # The `git ls-files -z` loads the files in the RubyGem that have been added into git.
  gemspec = File.basename(__FILE__)
  spec.files = IO.popen(%w[git ls-files -z], chdir: __dir__, err: IO::NULL) do |ls|
    ls.readlines("\x0", chomp: true).reject do |f|
      (f == gemspec) ||
        f.start_with?(*%w[bin/ test/ spec/ features/ .git .github appveyor Gemfile])
    end
  end
  spec.require_paths = %w[lib]
  spec.extensions = %w[ext/mmap/extconf.rb]
  spec.metadata['rubygems_mfa_required'] = 'true'
end
