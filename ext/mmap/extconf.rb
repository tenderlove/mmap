#!/usr/bin/env ruby
# frozen_string_literal: true

ARGV.map! { |x| x.sub(/^--with-mmap-prefix=/, '--with-mmap-dir=') }

require 'mkmf'

dir_config('mmap')

%w[lstrip match insert casecmp].each do |func|
  $CFLAGS += " -DHAVE_RB_STR_#{func.upcase}" if ''.respond_to?(func)
end

have_func 'rb_fstring_new'
has_semctl = have_func 'semctl', 'sys/sem.h'
has_shmctl = have_func 'shmctl', 'sys/shm.h'

warn "\tIPC will not be available" if enable_config('ipc') && !(has_semctl && has_shmctl)

create_makefile 'mmap/mmap'
