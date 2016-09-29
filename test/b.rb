#!/usr/bin/ruby
$LOAD_PATH.unshift "."
require "mmap"
PAGESIZE = 4096
f = File.open("aa", "w")
f.write("\0" * PAGESIZE)
f.write("b.rb")
f.write("\0" * PAGESIZE)
f.close
m = Mmap.new("aa", "w", "offset" => 0)
p m.size == "b.rb".size + 2 * PAGESIZE
p m.scan(/[a-z.]+/) == ["b.rb"]
p m.index("b.rb") == PAGESIZE
p m.rindex("b.rb") == PAGESIZE
p m.sub!(/[a-z.]+/, "toto") == m
p m.scan(/[a-z.]+/) == ["toto"]
begin
   m.sub!(/[a-z.]+/, "alpha")
   puts "not OK must give an error"
rescue
   puts "OK : #$!"
end
m.munmap
m = Mmap.new("aa", "rw")
p m.index("toto") == PAGESIZE
p m.sub!(/([a-z.]+)/, "alpha") == m
p $& == "toto"
p $1 == "toto"
p m.index("toto") == nil
p m.index("alpha") == PAGESIZE
p m.size == 5 + 2 * PAGESIZE
m.gsub!(/\0/, "X")
p m.size == 5 + 2 * PAGESIZE
