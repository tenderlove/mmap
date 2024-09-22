require 'mmap'
require 'fileutils'
require 'tempfile'
require 'minitest/autorun'

class TestMmap < Minitest::Test
  EXT_DIR = File.expand_path(File.join(File.dirname(__FILE__), '..', 'ext', 'mmap'))

  def setup
    @tmp = Dir.tmpdir
    FileUtils.cp(File.join(EXT_DIR, 'mmap.c'), @tmp)

    @mmap_c = File.join(@tmp, 'mmap.c')

    @mmap = Mmap.new(@mmap_c, 'rw')
    @str  = File.read @mmap_c
  end

  def teardown
    @mmap.unmap
    aa = File.join(@tmp, 'aa')
    bb = File.join(@tmp, 'bb')
    FileUtils.rm_f(aa)
    FileUtils.rm_f(bb)
  end

  def internal_read
    File.readlines(@mmap_c, nil)[0]
  end

  def test_inspect
    assert @mmap.inspect
  end

  # Make sure clone raises.  Cloning would be bad.
  def test_clone
    assert_raises(TypeError) do
      @mmap.clone
    end
  end

  # Make sure dup raises.  Cloning would be bad.
  def test_dup
    assert_raises(TypeError) do
      @mmap.dup
    end
  end

  def test_length
    assert_equal(@mmap.length, @str.length, '<lenght>')
  end

  def test_simple_aref
    assert_equal(@str[10], @mmap[10], '<aref>')
  end

  def test_aref
    max = @str.size * 2
    72.times do
      ran1 = rand(max)
      assert_same_result(@str[ran1], @mmap[ran1], '<aref>')
      assert_same_result(@str[-ran1], @mmap[-ran1], '<aref>')
      ran2 = rand(max)
      assert_same_result(@str[ran1, ran2],     @mmap[ran1, ran2], '<double aref>')
      assert_same_result(@str[-ran1, ran2],    @mmap[-ran1, ran2], '<double aref>')
      assert_same_result(@str[ran1, -ran2],    @mmap[ran1, -ran2], '<double aref>')
      assert_same_result(@str[-ran1, -ran2],   @mmap[-ran1, -ran2], '<double aref>')
      assert_same_result(@str[ran1..ran2],   @mmap[ran1..ran2], '<double aref>')
      assert_same_result(@str[-ran1..ran2],  @mmap[-ran1..ran2], '<double aref>')
      assert_same_result(@str[ran1..-ran2],  @mmap[ran1..-ran2], '<double aref>')
      assert_same_result(@str[-ran1..-ran2], @mmap[-ran1..-ran2], '<double aref>')
    end
    assert_same_result(@str[/random/], @mmap[/random/], '<aref regexp>')
    assert_same_result(@str[/real/],   @mmap[/real/], '<aref regexp>')
    assert_same_result(@str[/none/],   @mmap[/none/], '<aref regexp>')
  end

  def assert_same_result(expected, actual, msg)
    expected.nil? ? assert_nil(actual, msg) : assert_equal(expected, actual, msg)
  end

  def internal_aset(a, b = nil, c = true)
    access = if b
               repl = ''
               rand(12).times do
                 repl << (rand(65..89))
               end
               if c
                 "[a, b] = '#{repl}'"
               else
                 "[a .. b] = '#{repl}'"
               end
             else
               "[a] = #{rand(65..89)}.chr"
             end
    begin
      eval "@str#{access}"
    rescue IndexError, RangeError
      begin
        eval "@mmap#{access}"
      rescue IndexError, RangeError
      else
        flunk('*must* fail with IndexError')
      end
    else
      eval "@mmap#{access}"
    end
    assert_equal(@mmap.to_str, @str, '<internal aset>')
  end

  def test_aset
    @mmap[/...../] = 'change it'
    @str[/...../] = 'change it'
    assert_equal(@mmap.to_str, @str, 'aset regexp')
    @mmap['ge i'] = 'change it'
    @str['ge i'] = 'change it'
    assert_equal(@mmap.to_str, @str, 'aset regexp')
    max = @str.size * 2
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
  end

  def internal_slice(a, b = nil, c = true)
    access = if b
               if c
                 '.slice!(a, b)'
               else
                 '.slice!(a .. b)'
               end
             else
               '.slice!(a)'
             end
    begin
      eval "@str#{access}"
    rescue IndexError, RangeError
      begin
        eval "@mmap#{access}"
      rescue IndexError, RangeError
      else
        flunk('*must* fail with IndexError')
      end
    else
      eval "@mmap#{access}"
    end
    assert_equal(@mmap.to_str, @str, '<internal aset>')
  end

  def test_slice
    max = @str.size * 2
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
  end

  def test_easy_sub!
    assert_equal(@mmap.index('rb_raise'), @mmap.index('rb_raise'), '<index>')
  end

  def test_each_line
    actual = []
    expected = []
    @mmap.each_line { |line| actual << line }
    @str.each_line { |line| expected << line }
    assert_equal expected, actual

    actual = []
    expected = []
    @mmap.each_line('in') { |line| actual << line }
    @str.each_line('in') { |line| expected << line }
    assert_equal expected, actual
  end

  def test_each
    assert_equal @str.bytes, @mmap.to_a
  end

  def test_reg
    assert_equal(@str.scan('include'), @mmap.scan('include'), '<scan>')
    assert_equal(@mmap.index('rb_raise'), @mmap.index('rb_raise'), '<index>')
    assert_equal(@mmap.rindex('rb_raise'), @mmap.rindex('rb_raise'), '<rindex>')
    assert_equal(@mmap.index(/rb_raise/), @mmap.index(/rb_raise/), '<index>')
    assert_equal(@mmap.rindex(/rb_raise/), @mmap.rindex(/rb_raise/), '<rindex>')
    ('a'..'z').each do |i|
      assert_equal(@mmap.index(i), @str.index(i), '<index>')
      assert_equal(@mmap.rindex(i), @str.rindex(i), '<rindex>')
      assert_equal(@mmap.index(i), @str.index(/#{i}/), '<index>')
      assert_equal(@mmap.rindex(i), @str.rindex(/#{i}/), '<rindex>')
    end
    @mmap.sub!('GetMmap', 'XXXX')
    @str.sub!('GetMmap', 'XXXX')
    assert_equal(@str, @mmap.to_str, '<after sub!>')
    @mmap.gsub!('GetMmap', 'XXXX')
    @str.gsub!('GetMmap', 'XXXX')
    assert_equal(@mmap.to_str, @str, '<after gsub!>')
    @mmap.gsub!('YYYY', 'XXXX')
    @str.gsub!('YYYY', 'XXXX')
    assert_equal(@mmap.to_str, @str, '<after gsub!>')
    assert_equal(@mmap.split(/\w+/), @str.split(/\w+/), '<split>')
    assert_equal(@mmap.split(/\W+/), @str.split(/\W+/), '<split>')
    assert_equal(@mmap.crypt('abc'), @str.crypt('abc'), '<crypt>')
  end

  def internal_modify idmod, *args
    if (res = @str.method(idmod)[*args])
      assert_equal(@mmap.method(idmod)[*args].to_str, res, "<#{idmod}>")
    else
      assert_equal(@mmap.method(idmod)[*args], res, "<#{idmod}>")
    end
  end

  def test_modify
    skip 'pending'
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

  def test_iterate
    mmap = []
    @mmap.each_byte { |l| mmap << l }
    str = []
    @str.each_byte { |l| str << l }
    assert_equal(mmap, str, '<each_byte>')
  end

  def test_concat
    [@mmap, @str].each do |l|
      l << 'bc'
      l << 12
      l << 'ab'
    end
    assert_equal(@mmap.to_str, @str, '<<')
    assert_raises(TypeError) { @mmap << 456 }
  end

  def test_extend
    @mmap.extend(4096)
    assert_equal(@mmap.to_str, @str, 'extend')
    return unless @str.respond_to?(:insert)

    10.times do
      pos = rand(@mmap.size)
      str = 'XX' * rand(66)
      @str.insert(pos, str)
      @mmap.insert(pos, str)
      assert_equal(@mmap.to_str, @str, 'insert')
    end
  end

  def test_msync
    3.times do |_i|
      [@mmap, @str].each { |l| l << ('x' * 4096) }
      str = internal_read
      next unless str != @mmap.to_str

      @mmap.msync
      assert_equal(@mmap.to_str, internal_read, 'msync')
      break
    end
  end

  def test_protect
    assert_equal(@mmap, @mmap.protect('w'), 'protect')
    assert_equal('a', @mmap[12] = 'a', 'affect')
    @str[12] = 'a'
    assert_equal(@mmap.to_str, @str, 'protect')
    assert_raises(TypeError) { @mmap << 'a' }
    assert_equal(@mmap, @mmap.protect('r'), 'protect')
    assert_raises(RuntimeError) { @mmap[12] = 'a' }
    assert_raises(RuntimeError) { @mmap.protect('rw') }
  end

  def test_anonymous
    return unless defined?(Mmap::MAP_ANONYMOUS)

    assert_kind_of(Mmap, @mmap =
                           Mmap.new(nil, 'length' => 8192, 'offset' => 12,
                                         'increment' => 1024, 'initialize' => ' '))
    @str = ' ' * 8192
    1024.times do
      pos = rand(8192)
      @mmap[pos] = @str[pos] = rand(32..95).chr
    end
    assert_equal(@mmap.to_str, @str, 'insert anonymous')
    assert_raises(IndexError) { @mmap[12_345] = 'a' }
    assert_raises(TypeError) { @mmap << 'a' }
  end

  def test_fileno
    @mmap = Mmap.new(File.new(@mmap_c, 'r+'), 'rw')
    test_aref
    @mmap[12] = '3'
    @str[12] = '3'
    assert_equal(@mmap.to_str, @str, 'insert io')
    assert_equal(0, @mmap <=> @str, 'cmp')
    assert_raises(TypeError) { @mmap[12] = 'ab' }
    @mmap.freeze
    if @str.respond_to?(:match)
      assert_equal(@str.match('rb_match_busy').offset(0),
                   @mmap.match('rb_match_busy').offset(0), 'match')
      assert_equal(@str.match(/rb_../).offset(0),
                   @mmap.match(/rb_../).offset(0), 'match')
      assert_same_result(@str.match('rb_match_buzy'),
                         @mmap.match('rb_match_buzy'), 'no match')
      assert_equal(@str =~ /rb_match_busy/,
                   @mmap =~ /rb_match_busy/, 'match')
      assert_same_result(@str =~ /rb_match_buzy/,
                         @mmap =~ /rb_match_buzy/, 'no match')
    end
    assert_raises(RuntimeError) { @mmap[12] = 'a' }
  end

  def test_frozen
    @mmap.freeze
    assert_raises FrozenError do
      @mmap.sub!('GetMmap', 'XXXX')
      @str.sub!('GetMmap', 'XXXX')
    end
  end

  def test_div
    string = 'azertyuiopqsdfghjklm'
    assert_kind_of(Mmap, m0 = Mmap.new("#{@tmp}/aa", 'a'), 'new a')
    File.open("#{@tmp}/bb", 'w') { |f| f.puts 'aaa' }
    assert_kind_of(Mmap, m1 = Mmap.new("#{@tmp}/bb", 'w'), 'new a')
    assert_equal(true, m0.empty?, 'empty')
    assert_equal(true, m1.empty?, 'empty')
    assert_equal(m0, m0 << string, '<<')
    assert_equal(m1, m1 << string, '<<')
    assert_equal(false, m0.empty?, 'empty')
    assert_equal(false, m1.empty?, 'empty')
    assert_equal(true, m0 == m1, '==')
    if string.respond_to?(:casecmp)
      assert_equal(0, m0.casecmp(string.upcase), 'casecmp')
      assert_equal(0, m0.casecmp(m1), 'casecmp')
    end
    assert_equal(true, m0 === m1, '===')
    assert_equal(false, m0 === string, '===')
    assert_equal(true, m0.eql?(m1), '.eql?')
    assert_equal(true, m1.eql?(m0), '.eql?')
    assert_equal(false, m1.eql?(string), '.eql?')
    assert_equal(m0.hash, m1.hash, 'hash')
    assert_equal(true, m0.include?('azert'), 'include')
    assert_equal(false, m1.include?('aqert'), 'include')
    i = 0
    m0.scan(/./) do |c|
      assert_equal(c, string[i, 1], 'scan')
      i += 1
    end
    assert_nil(m0.munmap, 'munmap')
    assert_nil(m1.munmap, 'munmap')
  end

  def test_other
    test_div
    return unless File.exist?("#{@tmp}/aa")

    string = 'azertyuiopqsdfghjklm'
    assert_kind_of(Mmap, m0 = Mmap.new("#{@tmp}/aa", 'r'), 'new r')
    assert_equal(string, m0.to_str, 'content')
    assert_raises(RuntimeError) { m0[0] = 12 }
    assert_raises(RuntimeError) { m0 << 12 }
    assert_nil(m0.munmap, 'munmap')
    return unless defined?(Mmap::MAP_ANONYMOUS)

    assert_raises(ArgumentError) { Mmap.new(nil, 'w') }
    assert_kind_of(Mmap, m0 = Mmap.new(nil, 12), 'new w')
    assert_equal(false, m0.empty?, 'empty')
    assert_equal('a', m0[0] = 'a', 'set')
    assert_raises(TypeError) { m0 << 12 }
    if defined?(Mmap::MADV_DONTNEED)
      assert_nil(m0.advise(Mmap::MADV_DONTNEED), 'advise')
      assert_equal('a', m0[0, 1], 'get')
    end
    assert_equal(m0, m0.sub!(/./) { 'y' }, 'sub')
    assert_equal(m0, m0.gsub!(/./) { 'x' }, 'gsub')
    assert_equal('x' * 12, m0.to_str, 'retrieve')
    assert_equal('ab', m0[1..2] = 'ab', 'range')
    assert_raises(TypeError) { m0[1..2] = 'abc' }
    assert_raises(ArgumentError) { m0.lock }
    assert_raises(ArgumentError) { Mmap.lockall(0) }
    assert_nil(m0.munmap, 'munmap')
  end

  def test_insert_integer
    m = @mmap
    s = m.size
    m.insert(0, 1.chr)
    assert_equal(s + 1, m.size)
    assert_equal(1, m[0].ord)

    m[0, 4] = "\x01\x01\x01\x01"
    assert_equal("\x01", m[0])
    m[0] = 0
    assert_equal("\x00", m[0])
    m[0] = 1
    assert_equal("\x01", m[0])
  end

  def test_insert_frozen
    m = @mmap
    @mmap.freeze
    assert_raises FrozenError do
      m.insert(0, 1.chr)
    end
  end
end
