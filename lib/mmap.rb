# The Mmap class implement memory-mapped file objects
# 
# Most of these methods have the same syntax than the methods of String
#
# === WARNING
# === The variables $' and $` are not available with gsub! and sub!
class Mmap
   include Comparable
   include Enumerable
   class << self
      
      #disable paging of all pages mapped. <em>flag</em> can be 
      #<em>Mmap::MCL_CURRENT</em> or <em>Mmap::MCL_FUTURE</em>
      #
      def  lockall(flag)
      end
      
      #create a new Mmap object
      #
      #* <em>file</em>
      #
      #  Pathname of the file, if <em>nil</em> is given an anonymous map
      #  is created <em>Mmanp::MAP_ANON</em>
      #
      #* <em>mode</em>
      #
      #  Mode to open the file, it can be "r", "w", "rw", "a"
      #
      #* <em>protection</em>
      #
      #  specify the nature of the mapping
      #
      #  * <em>Mmap::MAP_SHARED</em>
      #
      #    Creates a mapping that's shared with all other processes 
      #    mapping the same areas of the file. 
      #    The default value is <em>Mmap::MAP_SHARED</em>
      #
      #  * <em>Mmap::MAP_PRIVATE</em>
      #
      #    Creates a private copy-on-write mapping, so changes to the
      #    contents of the mmap object will be private to this process
      #
      #* <em>options</em>
      #
      #  Hash. If one of the options <em>length</em> or <em>offset</em>
      #  is specified it will not possible to modify the size of
      #  the mapped file.
      #
      #  length:: maps <em>length</em> bytes from the file
      #
      #  offset:: the mapping begin at <em>offset</em>
      #
      #  advice:: the type of the access (see #madvise)
      #
      #
      def  new(file, mode = "r", protection = Mmap::MAP_SHARED, options = {})
      end
      
      #reenable paging
      #
      def  unlockall
      end
   end
   
   #add <em>count</em> bytes to the file (i.e. pre-extend the file) 
   #
   def  extend(count)
   end
   
   #<em>advice</em> can have the value <em>Mmap::MADV_NORMAL</em>,
   #<em>Mmap::MADV_RANDOM</em>, <em>Mmap::MADV_SEQUENTIAL</em>,
   #<em>Mmap::MADV_WILLNEED</em>, <em>Mmap::MADV_DONTNEED</em>
   #
   def  madvise(advice)
   end
   
   #change the mode, value must be "r", "w" or "rw"
   #
   def  mprotect(mode)
   end
   
   #disable paging
   #
   def  mlock
   end
   
   #flush the file
   #
   def  msync
   end
   #same than <em> msync</em>
   def  flush
   end
   
   #reenable paging
   #
   def  munlock
   end
   
   #terminate the association
   def  munmap
   end
   #
   #=== Other methods with the same syntax than for the class String
   #
   #
   
   #comparison
   #
   def  ==(other) 
   end
   
   #comparison
   #
   def  >(other)
   end
   
   #comparison
   #
   def  >=(other)
   end
   
   #comparison
   #
   def  <(other)
   end
   
   #comparison
   #
   def  <=(other)
   end
   
   #used for <em>case</em> comparison
   #
   def  ===(other)
   end
   
   #append <em>other</em> to <em>self</em>
   #
   def  <<(other)
   end
   
   #return an index of the match 
   #
   def  =~(other)
   end
   
   #Element reference - with the following syntax
   #
   #self[nth] 
   #
   #retrieve the <em>nth</em> character
   #
   #self[start..last]
   #
   #return a substring from <em>start</em> to <em>last</em>
   #
   #self[start, length]
   #
   #return a substring of <em>lenght</em> characters from <em>start</em> 
   #
   def  [](args)
   end
   
   
   # Element assignement - with the following syntax
   #
   # self[nth] = val
   #
   # change the <em>nth</em> character with <em>val</em>
   #
   # self[start..last] = val
   #
   # change substring from <em>start</em> to <em>last</em> with <em>val</em>
   #
   # self[start, len] = val
   #
   # replace <em>length</em> characters from <em>start</em> with <em>val</em>.
   #
   def  []=(args) 
   end
   
   #comparison : return -1, 0, 1
   #
   def  self <=> other 
   end
   
   # only with ruby >= 1.7.1
   def  casecmp(other)
   end
   
   #append the contents of <em>other</em>
   #
   def  concat(other) 
   end
   
   #change the first character to uppercase letter
   #
   def  capitalize!
   end
   
   #chop off the last character
   #
   def  chop! 
   end
   
   #chop off the  line ending character, specified by <em>rs</em>
   #
   def  chomp!(rs = $/) 
   end
   
   #each parameter defines a set of character to count
   #
   def  count(o1 [, o2, ...])
   end
   
   #crypt with <em>salt</em> 
   #
   def  crypt(salt)
   end
   
   #delete every characters included in <em>str</em>
   #
   def  delete!(str) 
   end
   
   #change all uppercase character to lowercase character
   #
   def  downcase! 
   end
   
   #iterate on each byte
   #
   def  each_byte  
      yield char
   end
   
   #iterate on each line
   #
   def  each(rs = $/)  
      yield line
   end
   #same than <em> each</em>
   def  each_line(rs = $/)  
      yield line
   end
   
   #return <em>true</em> if the file is empty
   #
   def  empty? 
   end
   
   #freeze the current file 
   #
   def  freeze
   end
   
   #return <em>true</em> if the file is frozen
   #
   def  frozen 
   end
   
   #global substitution
   #
   #str.gsub!(pattern, replacement)        => str or nil
   #
   #str.gsub!(pattern) {|match| block }    => str or nil
   #
   def  gsub!(pattern, replacement = nil)
   end
   
   #return <em>true</em> if <em>other</em> is found
   #
   def  include?(other) 
   end
   
   #return the index of <em>substr</em> 
   #
   def  index(substr[, pos])
   end
   
   #insert <em>str</em> at <em>index</em>
   #
   def  insert(index, str) >= 1.7.1
   end
   
   #return the size of the file
   #
   def  length 
   end

   #convert <em>pattern</em> to a <em>Regexp</em> and then call
   #<em>match</em> on <em>self</em>
   def  match(pattern)
   end
   
   #reverse the content of the file 
   #
   def  reverse!
   end
   
   #return the index of the last occurrence of <em>substr</em>
   #
   def  rindex(substr[, pos]) 
   end
   
   #return an array of all occurence matched by <em>pattern</em> 
   #
   def  scan(pattern)
   end
   
   #iterate through the file, matching the <em>pattern</em>
   #
   def  scan(pattern)  
      yield str
   end
   
   #return the size of the file
   #
   def  size 
   end
   
   #same than <em>[]</em>
   #
   def  slice
   end
   
   #delete the specified portion of the file
   #
   def  slice!
   end
   
   #splits into a list of strings and return this array
   #
   def  split([sep[, limit]]) 
   end
   
   #squeezes sequences of the same characters which is included in <em>str</em>
   #
   def  squeeze!([str]) 
   end
   
   #removes leading and trailing whitespace
   #
   def  strip! 
   end

   #removes leading whitespace
   #
   def  lstrip! 
   end

   #removes trailing whitespace
   #
   def  rstrip! 
   end
   
   #substitution 
   #
   #str.sub!(pattern, replacement)        => str or nil
   #
   #str.sub!(pattern) {|match| block }    => str or nil
   #
   #
   def  sub!(pattern, replacement = nil)
   end
   
   #return a checksum
   #
   def  sum(bits = 16) 
   end
   
   #replaces all lowercase characters to uppercase characters, and vice-versa
   #
   def  swapcase! 
   end
   
   #translate the character from <em>search</em> to <em>replace</em> 
   #
   def  tr!(search, replace)
   end
   
   #translate the character from <em>search</em> to <em>replace</em>, then
   #squeeze sequence of the same characters 
   #
   def  tr_s!(search, replace) 
   end

   #replaces all lowercase characters to downcase characters
   #
   def upcase! 
   end


end
