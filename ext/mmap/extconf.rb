#!/usr/bin/ruby
ARGV.collect! {|x| x.sub(/^--with-mmap-prefix=/, "--with-mmap-dir=") }

require 'mkmf'

dir_config("mmap")

["lstrip", "match", "insert", "casecmp"].each do |func|
   if "aa".respond_to?(func)
      $CFLAGS += " -DHAVE_RB_STR_#{func.upcase}"
   end
end

have_func 'rb_fstring_new'

if enable_config("ipc")
   unless have_func("semctl") && have_func("shmctl")
      $stderr.puts "\tIPC will not be available"
   end
end

create_makefile "mmap/mmap"
