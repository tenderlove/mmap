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

begin
   make = open("Makefile", "a")
   make.puts "\ntest: $(DLLIB)"
   Dir.foreach('test') do |x|
      next if /^\./ =~ x || /(_\.rb|~)$/ =~ x
      next if FileTest.directory?(x)
      make.print "\truby test/#{x}\n"
   end
   if unknown
      make.print <<-EOT

unknown: $(DLLIB)
\t@echo "main() {}" > /tmp/a.c
\t$(CC) -static /tmp/a.c $(OBJS) $(CPPFLAGS) $(DLDFLAGS) $(LIBS) $(LOCAL_LIBS)
\t@-rm /tmp/a.c a.out

EOT
   end
   make.print <<-EOT
%.html: %.rd
\trd2 $< > ${<:%.rd=%.html}

   EOT
   make.print "HTML = mmap.html"
   doc = Dir['doc/*.rd']
   doc.each {|x| make.print " \\\n\t#{x.sub(/\.rd$/, '.html')}" }
   make.print "\n\nRDOC = doc/mmap.rb"
   make.puts
   make.print <<-EOF

rdoc: doc/doc/index.html

doc/doc/index.html: $(RDOC)
\t@-(cd doc; rdoc mmap.rb)

ri: doc/mmap.rb
\t@-(cd doc; rdoc -r mmap.rb)

ri-site: doc/mmap.rb
\t@-(cd doc; rdoc -R mmap.rb)

rd2: html

html: $(HTML)

   EOF
ensure
   make.close
end

