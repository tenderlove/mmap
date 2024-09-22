# frozen_string_literal: true

# The Mmap class implement memory-mapped file objects
#
# Most of these methods have the same syntax than the methods of String
#
# === WARNING
# === The variables $' and $` are not available with gsub! and sub!
require 'mmap/mmap'
require_relative 'mmap/version'

class Mmap
  include Comparable
  include Enumerable

  def clone # :nodoc:
    raise TypeError, "can't clone instance of #{self.class}"
  end

  def dup # :nodoc:
    raise TypeError, "can't dup instance of #{self.class}"
  end

  # call-seq: scan(pattern, &block)
  #
  # return an array of all occurence matched by <em>pattern</em>
  def scan(pattern, &block)
    to_str.scan(pattern, &block)
  end

  # Document-method: each_line
  #
  # call-seq:
  #    each(rs = $/, &block)
  #
  # iterate on each line
  def each_line(...)
    to_str.each_line(...)
  end

  # call-seq: each_byte(&block)
  #
  # iterate on each byte
  def each_byte(&block)
    to_str.each_byte(&block)
  end
  alias each each_byte

  private

  def process_options(options)
    options.each do |k, v|
      case k
      when 'length' then set_length v
      when 'offset' then set_offset v
      when 'advice' then set_advice v
      when 'increment' then set_increment v
      when 'initialize' # skip
      when 'ipc' then set_ipc v
      else
        warn "Unknown option #{k}"
      end
    end
  end
end
