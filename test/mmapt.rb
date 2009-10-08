#!/usr/bin/ruby
$LOAD_PATH.unshift *%w{.. . test}
require 'mmap'
require 'ftools'

$pathmm = $LOAD_PATH.find {|p| File.exist?(p + "/mmap.c") }
unless $pathmm
   $LOAD_PATH.each do |p|
      p p
      if p.gsub!(%r{/test\Z}, '/') &&
            File.exists?(p + '/mmap.c')
         $pathmm = p
         break
      end
   end
end
raise "unable to find mmap.c" unless $pathmm

load "#{$pathmm}/test/runit_.rb"

$mmap, $str = nil, nil

Inh = defined?(RUNIT) ? RUNIT : Test::Unit

$pathmm = $LOAD_PATH.find {|p| File.exist?(p + "/mmap.c") }
raise "unable to find mmap.c" unless $pathmm

Dir.mkdir("#{$pathmm}/tmp") unless FileTest.directory?("#{$pathmm}/tmp")

Dir["#{$pathmm}/tmp/*"].each do |f|
   File.unlink(f) if FileTest.file?(f)
end

class TestMmap < Inh::TestCase
   
   def internal_read
      File.readlines("#{$pathmm}/tmp/mmap", nil)[0]
   end

   def internal_init(io = false)
      $mmap.unmap if $mmap
      file = "#{$pathmm}/mmap.c"
      File.syscopy file, "#{$pathmm}/tmp/mmap"
      $str = internal_read
      if io
	 assert_kind_of(Mmap, $mmap = Mmap.new(File.new("#{$pathmm}/tmp/mmap", "r+"), "rw"), "<open io>")
      else
	 assert_kind_of(Mmap, $mmap = Mmap.new("#{$pathmm}/tmp/mmap", "rw"), "<open>")
      end
   end

   def test_00_init
      internal_init
      assert_equal($mmap.length, $str.length, "<lenght>")
   end

   def test_01_aref
      max = $str.size * 2
      72.times do
	 ran1 = rand(max)
	 assert_equal($mmap[ran1], $str[ran1], "<aref>");
	 assert_equal($mmap[-ran1], $str[-ran1], "<aref>");
	 ran2 = rand(max)
	 assert_equal($mmap[ran1, ran2], $str[ran1, ran2], "<double aref>");
	 assert_equal($mmap[-ran1, ran2], $str[-ran1, ran2], "<double aref>");
	 assert_equal($mmap[ran1, -ran2], $str[ran1, -ran2], "<double aref>");
	 assert_equal($mmap[-ran1, -ran2], $str[-ran1, -ran2], "<double aref>");
	 assert_equal($mmap[ran1 .. ran2], $str[ran1 .. ran2], "<double aref>");
	 assert_equal($mmap[-ran1 .. ran2], $str[-ran1 .. ran2], "<double aref>");
	 assert_equal($mmap[ran1 .. -ran2], $str[ran1 .. -ran2], "<double aref>");
	 assert_equal($mmap[-ran1 .. -ran2], $str[-ran1 .. -ran2], "<double aref>");
      end
      assert_equal($mmap[/random/], $str[/random/], "<aref regexp>")
      assert_equal($mmap[/real/], $str[/real/], "<aref regexp>")
      assert_equal($mmap[/none/], $str[/none/], "<aref regexp>")
   end

   def internal_aset(a, b = nil, c = true)
      access = if b
		  repl = ''
		  rand(12).times do
	              repl << (65 + rand(25))
                  end
		  if c 
		     "[a, b] = '#{repl}'"
		  else
		     "[a .. b] = '#{repl}'"
		  end
	       else
		  "[a] = #{(65 + rand(25))}"
	       end
      begin
	 eval "$str#{access}"
      rescue IndexError, RangeError
	 begin
	    eval "$mmap#{access}"
	 rescue IndexError, RangeError
	 else
	    flunk("*must* fail with IndexError")
	 end
      else
	 eval "$mmap#{access}"
      end
      assert_equal($mmap.to_str, $str, "<internal aset>")
   end

   def test_02_aset
      $mmap[/...../] = "change it"
      $str[/...../] = "change it"
      assert_equal($mmap.to_str, $str, "aset regexp")
      $mmap["ge i"] = "change it"
      $str["ge i"] = "change it"
      assert_equal($mmap.to_str, $str, "aset regexp")
      max = $str.size * 2
      72.times do
	 ran1 = rand(max)
	 internal_aset(ran1)
	 internal_aset(-ran1)
	 ran2 = rand(max)
	 internal_aset(ran1, ran2)
	 internal_aset(ran1, -ran2)
	 internal_aset(-ran1, ran2)
	 internal_aset(-ran1, -ran2)
	 internal_aset(ran1, ran2, false)
	 internal_aset(ran1, -ran2, false)
	 internal_aset(-ran1, ran2, false)
	 internal_aset(-ran1, -ran2, false)
      end
      internal_init
   end

   def internal_slice(a, b = nil, c = true)
      access = if b
		  if c 
		     ".slice!(a, b)"
		  else
		     ".slice!(a .. b)"
		  end
	       else
		  ".slice!(a)"
	       end
      begin
	 eval "$str#{access}"
      rescue IndexError, RangeError
	 begin
	    eval "$mmap#{access}"
	 rescue IndexError, RangeError
	 else
	    flunk("*must* fail with IndexError")
	 end
      else
	 eval "$mmap#{access}"
      end
      assert_equal($mmap.to_str, $str, "<internal aset>")
   end

   def test_03_slice
      max = $str.size * 2
      72.times do
	 ran1 = rand(max)
	 internal_slice(ran1)
	 internal_slice(-ran1)
	 ran2 = rand(max)
	 internal_slice(ran1, ran2)
	 internal_slice(ran1, -ran2)
	 internal_slice(-ran1, ran2)
	 internal_slice(-ran1, -ran2)
	 internal_slice(ran1, ran2, false)
	 internal_slice(ran1, -ran2, false)
	 internal_slice(-ran1, ran2, false)
	 internal_slice(-ran1, -ran2, false)
      end
      internal_init
   end

   def test_04_reg
      assert_equal($mmap.scan(/include/), $str.scan(/include/), "<scan>")
      assert_equal($mmap.index("rb_raise"), $str.index("rb_raise"), "<index>")
      assert_equal($mmap.rindex("rb_raise"), $str.rindex("rb_raise"), "<rindex>")
      assert_equal($mmap.index(/rb_raise/), $str.index(/rb_raise/), "<index>")
      assert_equal($mmap.rindex(/rb_raise/), $str.rindex(/rb_raise/), "<rindex>")
      ('a' .. 'z').each do |i|
	 assert_equal($mmap.index(i), $str.index(i), "<index>")
	 assert_equal($mmap.rindex(i), $str.rindex(i), "<rindex>")
	 assert_equal($mmap.index(i), $str.index(/#{i}/), "<index>")
	 assert_equal($mmap.rindex(i), $str.rindex(/#{i}/), "<rindex>")
      end
      $mmap.sub!(/GetMmap/, 'XXXX'); $str.sub!(/GetMmap/, 'XXXX')
      assert_equal($mmap.to_str, $str, "<after sub!>")
      $mmap.gsub!(/GetMmap/, 'XXXX'); $str.gsub!(/GetMmap/, 'XXXX')
      assert_equal($mmap.to_str, $str, "<after gsub!>")
      $mmap.gsub!(/YYYY/, 'XXXX'); $str.gsub!(/YYYY/, 'XXXX')
      assert_equal($mmap.to_str, $str, "<after gsub!>")
      assert_equal($mmap.split(/\w+/), $str.split(/\w+/), "<split>")
      assert_equal($mmap.split(/\W+/), $str.split(/\W+/), "<split>")
      assert_equal($mmap.crypt("abc"), $str.crypt("abc"), "<crypt>")
      internal_init
   end

   def internal_modify idmod, *args
      if res = $str.method(idmod)[*args]
	 assert_equal($mmap.method(idmod)[*args].to_str, res, "<#{idmod}>")
      else
	 assert_equal($mmap.method(idmod)[*args], res, "<#{idmod}>")
      end
   end

   def test_05_modify
      internal_modify(:reverse!)
      internal_modify(:upcase!)
      internal_modify(:downcase!)
      internal_modify(:capitalize!)
      internal_modify(:swapcase!)
      internal_modify(:strip!)
      internal_modify(:chop!)
      internal_modify(:chomp!)
      internal_modify(:squeeze!)
      internal_modify(:tr!, 'abcdefghi', '123456789')
      internal_modify(:tr_s!, 'jklmnopqr', '123456789')
      internal_modify(:delete!, 'A-Z')
   end

   def test_06_iterate
      internal_init
      mmap = []; $mmap.each {|l| mmap << l}
      str = []; $str.each {|l| str << l}
      assert_equal(mmap, str, "<each>")
      mmap = []; $mmap.each_byte {|l| mmap << l}
      str = []; $str.each_byte {|l| str << l}
      assert_equal(mmap, str, "<each_byte>")
   end

   def test_07_concat
      internal_init
      [$mmap, $str].each {|l| l << "bc"; l << 12; l << "ab"}
      assert_equal($mmap.to_str, $str, "<<")
      assert_raises(TypeError) { $mmap << 456 }
   end

   def test_08_extend
      $mmap.extend(4096)
      assert_equal($mmap.to_str, $str, "extend")
      if $str.respond_to?(:insert)
	 10.times do
	    pos = rand($mmap.size)
	    str = "XX" * rand(66)
	    $str.insert(pos, str)
	    $mmap.insert(pos, str)
	    assert_equal($mmap.to_str, $str, "insert")
	 end
      end
   end
   
   def test_09_msync
      3.times do |i|
	 [$mmap, $str].each {|l| l << "x" * 4096 }
	 str = internal_read
	 if str != $mmap.to_str
	    $mmap.msync
	    assert_equal($mmap.to_str, internal_read, "msync")
	    break
	 end
      end
   end

   def test_10_protect
      assert_equal($mmap, $mmap.protect("w"), "protect")
      assert_equal("a", $mmap[12] = "a", "affect")
      $str[12] = "a"
      assert_equal($mmap.to_str, $str, "protect")
      assert_raises(TypeError) { $mmap << "a" }
      assert_equal($mmap, $mmap.protect("r"), "protect")
      assert_raises(TypeError) { $mmap[12] = "a" }
      assert_raises(TypeError) { $mmap.protect("rw") }
   end

   def test_11_anonymous
      if defined?(Mmap::MAP_ANONYMOUS)
	 assert_kind_of(Mmap, $mmap = 
			Mmap.new(nil, "length" => 8192, "offset" => 12, 
				 "increment" => 1024, "initialize" => " "))
	 $str = " " * 8192
	 1024.times do
	    pos = rand(8192)
	    $mmap[pos] = $str[pos] = 32 + rand(64)
	 end
	 assert_equal($mmap.to_str, $str, "insert anonymous")
	 assert_raises(IndexError) { $mmap[12345] = "a" }
	 assert_raises(TypeError) { $mmap << "a" }
      end
   end

   def test_12_fileno
      internal_init(true)
      test_01_aref
      $mmap[12] = "3"; $str[12] = "3"
      assert_equal($mmap.to_str, $str, "insert io")
      assert_equal(0, $mmap <=> $str, "cmp")
      assert_raises(TypeError) { $mmap[12] = "ab" }
      $mmap.freeze
      if $str.respond_to?(:match)
	 assert_equal($str.match("rb_match_busy").offset(0), 
		      $mmap.match("rb_match_busy").offset(0), "match")
	 assert_equal($str.match(/rb_../).offset(0), 
		      $mmap.match(/rb_../).offset(0), "match")
	 assert_equal($str.match("rb_match_buzy"), 
		      $mmap.match("rb_match_buzy"), "no match")
 	 assert_equal($str =~ /rb_match_busy/, 
		      $mmap =~ /rb_match_busy/, "match")
 	 assert_equal($str =~ /rb_match_buzy/, 
		      $mmap =~ /rb_match_buzy/, "no match")
     end
      assert_raises(TypeError) { $mmap[12] = "a" }
   end

   def test_13_div
      string = "azertyuiopqsdfghjklm"
      assert_kind_of(Mmap, m0 = Mmap.new("#{$pathmm}/tmp/aa", "a"), "new a")
      File.open("#{$pathmm}/tmp/bb", "w") {|f| f.puts "aaa" }
      assert_kind_of(Mmap, m1 = Mmap.new("#{$pathmm}/tmp/bb", "w"), "new a")
      assert_equal(true, m0.empty?, "empty")
      assert_equal(true, m1.empty?, "empty")
      assert_equal(m0, m0 << string, "<<")
      assert_equal(m1, m1 << string, "<<")
      assert_equal(false, m0.empty?, "empty")
      assert_equal(false, m1.empty?, "empty")
      assert_equal(true, m0 == m1, "==")
      if string.respond_to?(:casecmp)
	 assert_equal(0, m0.casecmp(string.upcase), "casecmp")
	 assert_equal(0, m0.casecmp(m1), "casecmp")
      end
      assert_equal(true, m0 === m1, "===")
      assert_equal(false, m0 === string, "===")
      assert_equal(true, m0.eql?(m1), ".eql?")
      assert_equal(true, m1.eql?(m0), ".eql?")
      assert_equal(false, m1.eql?(string), ".eql?")
      assert_equal(m0.hash, m1.hash, "hash")
      assert_equal(true, m0.include?("azert"), "include")
      assert_equal(false, m1.include?("aqert"), "include")
      i = 0
      m0.scan(/./) {|c| assert_equal(c, string[i,1], "scan"); i += 1}
      assert_nil(m0.munmap, "munmap")
      assert_nil(m1.munmap, "munmap")
   end

   def test_14_other
      if File.exist?("#{$pathmm}/tmp/aa")
	 string = "azertyuiopqsdfghjklm"
	 assert_kind_of(Mmap, m0 = Mmap.new("#{$pathmm}/tmp/aa", "r"), "new r")
	 assert_equal(string, m0.to_str, "content")
	 assert_raises(TypeError) { m0[0] = 12 }
	 assert_raises(TypeError) { m0 << 12 }
	 assert_nil(m0.munmap, "munmap")
	 if defined?(Mmap::MAP_ANONYMOUS)
	    assert_raises(ArgumentError) { Mmap.new(nil, "w") }
	    assert_kind_of(Mmap, m0 = Mmap.new(nil, 12), "new w")
	    assert_equal(false, m0.empty?, "empty")
	    assert_equal("a", m0[0] = "a", "set")
	    assert_raises(TypeError) { m0 << 12 }
	    if defined?(Mmap::MADV_DONTNEED)
	       assert_nil(m0.advise(Mmap::MADV_DONTNEED), "advise")
	       assert_equal("a", m0[0,1], "get")
	    end
	    assert_equal(m0, m0.sub!(/./) { "y" }, "sub")
	    assert_equal(m0, m0.gsub!(/./) { "x" }, "gsub")
	    assert_equal("x" * 12, m0.to_str, "retrieve")   
	    assert_equal("ab", m0[1..2] = "ab", "range")
	    assert_raises(TypeError) { m0[1..2] = "abc" }
	    assert_raises(ArgumentError) { m0.lock }
	    assert_raises(ArgumentError) { Mmap::lockall(0) }
	    assert_nil(m0.munmap, "munmap")
	 end
      end
   end
end

if defined?(RUNIT)
   RUNIT::CUI::TestRunner.run(TestMmap.suite)
end

