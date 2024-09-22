# Mmap

The Mmap class implement memory-mapped file objects

## SYNOPSIS

```ruby
  require 'mmap'

  mmap = Mmap.new(__FILE__)
  mmap.advise(Mmap::MADV_SEQUENTIAL)

  mmap.each do |line|
    puts line
  end
```

## Installation

```
gem install mmap
```

## Documentation

```
rake docs
```


## Interface

**WARNING: The variables $' and $` are not available with gsub! and sub!**

### SuperClass

`Object`

### Included Modules

- `Comparable`
- `Enumerable`

### Class Methods

- `lockall(flag)`:
      disable paging of all pages mapped. `|flag|` can be
      `Mmap::MCL_CURRENT` or `Mmap::MCL_FUTURE`

- `new(file, mode = "r", protection = Mmap::MAP_SHARED, options = {})`
  `new(nil, length, protection = Mmap::MAP_SHARED, options = {})`:
    create a new Mmap object

  - `file`:  Pathname of the file, if `nil` is given an anonymous map
      is created `Mmanp::MAP_ANON`

  - `mode`:  Mode to open the file, it can be `r`, `w`, `rw`, `a`

  - `protection`: specify the nature of the mapping

    - `Mmap::MAP_SHARED`:
             Creates a mapping that's shared with all other processes
             mapping the same areas of the file.
             The default value is `Mmap::MAP_SHARED`

    - `Mmap::MAP_PRIVATE`:
             Creates a private copy-on-write mapping, so changes to the
             contents of the mmap object will be private to this process

  - `options`:
      Hash. If one of the options `length` or `offset`
      is specified it will not possible to modify the size of
      the mapped file.

    - `length`: Maps `length` bytes from the file
    - `offset`: The mapping begin at `offset`
    - `advice`: The type of the access (see `#madvise`)

- `unlockall`: reenable paging

### Instance Methods

- `extend(count)`: add `count` bytes to the file (i.e. pre-extend the file)

- `madvise(advice)`: `advice` can have the value `Mmap::MADV_NORMAL`,
     `Mmap::MADV_RANDOM`, `Mmap::MADV_SEQUENTIAL`,
     `Mmap::MADV_WILLNEED`, `Mmap::MADV_DONTNEED`

- `mprotect(mode)`: change the mode, value must be `r`, `w` or `rw`

- `mlock`: disable paging

- `msync`
  `flush`: flush the file

- `munlock`: reenable paging

- `munmap`: terminate the association

### Other methods with the same syntax than for the class String


- `self == other`:     comparison

- `self > other`:     comparison

- `self >= other`:     comparison

- `self < other`:     comparison

- `self <= other`:     comparison

- `self === other`:     used for `case` comparison

- `self << other`:     append `other` to `self`

- `self =~ other`:     return an index of the match

- `self[nth]`:     retrieve the `nth` character

- `self[start..last]`:     return a substring from `start` to `last`

- `self[start, length]`:     return a substring of `lenght` characters from `start`

- `self[nth] = val`:     change the `nth` character with `val`

- `self[start..last] = val`:     change substring from `start` to `last` with `val`

- `self[start, len] = val`:     replace `length` characters from `start` with `val`.

- `self <=> other`:     comparison : return -1, 0, 1

- `casecmp(other)   >= 1.7.1`:
- `concat(other)`:     append the contents of `other`

- `capitalize!`:     change the first character to uppercase letter

- `chop!`:     chop off the last character

- `chomp!([rs])`:     chop off the  line ending character, specified by `rs`

- `count(o1 [, o2, ...])`:     each parameter defines a set of character to count

- `crypt(salt)`:     crypt with `salt`

- `delete!(str)`:     delete every characters included in `str`

- `downcase!`:     change all uppercase character to lowercase character

- `each_byte {|char|...}`:     iterate on each byte

- `each([rs]) {|line|...}`: - `each_line([rs]) {|line|...}`:     iterate on each line

- `empty?`:     return `true` if the file is empty

- `freeze`:     freeze the current file

- `frozen`:     return `true` if the file is frozen

- `gsub!(pattern, replace)`:     global substitution

- `gsub!(pattern) {|str|...}`:     global substitution

- `include?(other)`:     return `true` if `other` is found

- `index(substr[, pos])`:     return the index of `substr`

- `insert(index, str) >= 1.7.1`:     insert `str` at `index`

- `length`:     return the size of the file

- `reverse!`:     reverse the content of the file

- `rindex(substr[, pos])`:     return the index of the last occurrence of `substr`

- `scan(pattern)`:     return an array of all occurence matched by `pattern`

- `scan(pattern) {|str| ...}`:     iterate through the file, matching the `pattern`

- `size`:     return the size of the file

- `slice`:     same than `[]`

- `slice!`:     delete the specified portion of the file

- `split([sep[, limit]])`:     splits into a list of strings and return this array

- `squeeze!([str])`:     squeezes sequences of the same characters which is included in `str`

- `strip!`:     removes leading and trailing whitespace

- `sub!(pattern, replace)`:     substitution

- `sub!(pattern) {|str| ...}`:     substitution

- `sum([bits])`:     return a checksum

- `swapcase!`:     replaces all lowercase characters to uppercase characters, and vice-versa

- `tr!(search, replace)`:     translate the character from `search` to `replace`

- `tr_s!(search, replace)`:     translate the character from `search` to `replace`, then
    squeeze sequence of the same characters

- `upcase!`:     replaces all lowercase characters to downcase characters


## Copying

This extension module is copyrighted free software by Guy Decoux

You can redistribute it and/or modify it under the same term as
Ruby.

Guy Decoux <ts@moulon.inra.fr>
