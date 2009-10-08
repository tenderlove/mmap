#:nodoc:all
begin
   require 'test/unit'
rescue LoadError 
   require 'runit/testcase'
   require 'runit/cui/testrunner'

   module RUNIT
      module Assert
	 def assert_raises(error, message = nil)
	    begin
	       yield
	    rescue error
	       assert(true, message)
	    rescue
	       assert_fail("must fail with #{error} : #{string}")
	    else
	       assert_fail("*must* fail : #{string}")
	    end
	 end
	 def flunk(message = "")
	    assert_fail(message)
	 end
      end
   end
end


if RUBY_VERSION > "1.7"
   class Array
      alias indices select
   end
   class Hash
      alias indexes select
   end
   module BDB
      class Common
	 alias indexes select
      end

      class Recnum
	 alias indices select
      end
   end
end
