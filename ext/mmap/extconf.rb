#!/usr/bin/ruby
ARGV.collect! {|x| x.sub(/^--with-mmap-prefix=/, "--with-mmap-dir=") }

require 'mkmf'

if unknown = enable_config("unknown")
   libs = if CONFIG.key?("LIBRUBYARG_STATIC")
	     Config::expand(CONFIG["LIBRUBYARG_STATIC"].dup).sub(/^-l/, '')
	  else
	     Config::expand(CONFIG["LIBRUBYARG"].dup).sub(/^lib([^.]*).*/, '\\1')
	  end
   unknown = find_library(libs, "ruby_init", 
			  Config::expand(CONFIG["archdir"].dup))
end

dir_config("mmap")

["lstrip", "match", "insert", "casecmp"].each do |func|
   if "aa".respond_to?(func)
      $CFLAGS += " -DHAVE_RB_STR_#{func.upcase}"
   end
end

if enable_config("ipc")
   unless have_func("semctl") && have_func("shmctl")
      $stderr.puts "\tIPC will not be available"
   end
end

$CFLAGS += " -DRUBYLIBDIR='\"#{CONFIG['rubylibdir']}\"'"

create_makefile "mmap"
