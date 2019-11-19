# The Mmap class implement memory-mapped file objects
#
# Most of these methods have the same syntax than the methods of String
#
# === WARNING
# === The variables $' and $` are not available with gsub! and sub!
require 'mmap/mmap'

class Mmap
  include Comparable
  include Enumerable

  VERSION = '0.2.6'

  def clone # :nodoc:
    raise TypeError, "can't clone instance of #{self.class}"
  end

  def dup # :nodoc:
    raise TypeError, "can't dup instance of #{self.class}"
  end

  private

  def process_options(options)
    options.each do |k, v|
      case k
      when "length" then set_length v
      when "offset" then set_offset v
      when "advice" then set_advice v
      when "increment" then set_increment v
      when "initialize" # skip
      when "ipc" then set_ipc v
      else
        warn "Unknown option #{k}"
      end
    end
  end
end
