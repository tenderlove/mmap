# frozen_string_literal: true

require 'bundler/gem_tasks'
require 'rake/testtask'

Rake::TestTask.new do |t|
  t.libs << 'lib' # Include lib directory in the load path
  t.test_files = FileList['test/**/test_*.rb'] # Specify the test files
  t.warning = true # Enable ruby warnings
end

require 'rubocop/rake_task'

RuboCop::RakeTask.new

require 'rake/extensiontask'

Rake::ExtensionTask.new 'mmap' do |ext|
  ext.lib_dir = 'lib/mmap'
end

task default: %i[test rubocop]
