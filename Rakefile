require 'bundler/setup'
require 'bundler/gem_tasks'
require 'rake/testtask'
require 'rake/extensiontask'

task :build   => :compile
task :default => :test

Rake::ExtensionTask.new('mmap')
Rake::TestTask.new
