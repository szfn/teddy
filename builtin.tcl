proc kill_line {} {
   go :1
   mark
   move next line
   cb cut
}

proc shell_perform_redirection {redirection} {
   set redirected_descriptor [dict get $redirection redirected_descriptor]
   set open_direction [dict get $redirection open_direction]
   set target [dict get $redirection target]
      
   switch -exact $open_direction {
      ">" {
         if {$redirected_descriptor eq ""} {set redirected_descriptor 1}
         #puts "Sending $redirected_descriptor to $target"
         set fd [fdopen -wronly $target]
         fddup2 $fd $redirected_descriptor
         fdclose $fd
      }
      ">&" {
         if {$redirected_descriptor eq ""} {set redirected_descriptor 1}
         fddup2 $target $redirected_descriptor
      }
      "<" {
         if {$redirected_descriptor eq ""} {set redirected_descriptor 0}
         set fd [fdopen -rdonly $target]
         fddup2 $fd $redirected_descriptor
         fdclose $fd
      }
      "<&" {
         if {$redirected_descriptor eq ""} {set redirected_descriptor 0}
         fddup2 $target $redirected_descriptor
      }
      ">>" {
         if {$redirected_descriptor eq ""} {set redirected_descriptor 1}
         set fd [fdopen -wronly -append $target]
         fddup2 $fd $redirected_descriptor
         fdclose $fd
      }
   }
}

proc shell_eat {args i specialVarName normalVarName} {
   set special {}
   set normal {}
   
   for {} {$i < [llength $args]} {incr i} {
      set cur [lindex $args $i]
      if {$cur eq "&&"} { break }
      if {$cur eq "||"} { break }
      if {$cur eq "|"} { break }
      set isspecial [regexp {^([0-9]*)(>|>&|<&|<|>>)(.*)$} $cur -> redirected_descriptor open_direction target]
      if {$isspecial} {
         lappend special [dict create redirected_descriptor $redirected_descriptor open_direction $open_direction target $target]
      } else {
         if {[string first * $cur] >= 0} {
			# Perform autoglobbing
			lappend normal {*}[glob $cur]
         } else {
			lappend normal $cur
         }
      }
   }
   
   upvar $specialVarName specialVar
   upvar $normalVarName normalVar
   set specialVar $special
   set normalVar $normal
      
   return $i
}

proc shell_child_code {special normal pipe} {
   # child code, make redirects and exec
   
   #puts "message from child code"
   
   if {$pipe ne ""} {
      # default ouput of this process will be pipe's output side
      fdclose [lindex $pipe 0]
      fddup2 [lindex $pipe 1] 1
      fdclose [lindex $pipe 1]
   }
   
   for {set i 0} {$i < [llength $special]} {incr i} {
      shell_perform_redirection [lindex $special $i]
   }
   
   #puts "Executing $normal"
   posixexec {*}$normal
   
   posixexit -1
}

proc shell {args} {
   global backgrounded
   if {!$backgrounded} {
      error "shell called on a non backgrounded interpreter"
   }

   set i 0
 
   while {$i < [llength $args]} {
      set i [shell_eat $args $i special normal]
      
      set pipe ""
      if {$i < [llength $args]} {
         if {[lindex $args $i] eq "|"} {
			set pipe [fdpipe]
         }
      }
   
      set pid [posixfork]
      if {$pid < 0} {
         error "fork failed in 'shell'"
      }
      
      if {$pid == 0} {
         shell_child_code $special $normal $pipe
      }
   
      # parent code, wait and exit
      
      if {$pipe ne ""} {
         # new default standard input is pipe's input side
         fdclose [lindex $pipe 1]
         fddup2 [lindex $pipe 0] 0
         fdclose [lindex $pipe 0]
      }
         
      if {$i >= [llength $args]} {
         #puts "waiting for $pid"
         set r [posixwaitpid $pid]
         #puts "wait ended <$r>"
         return [lindex $r 1]
      }
      
      switch -exact [lindex $args $i] {
         "&&" {
			#puts "Processing AND $pid"
			set r [posixwaitpid $pid]
			#puts "Child returned [lindex $r 1]"
			if {[lindex $r 1] != 0} {
               # can not continue because last execution failed
               return [lindex $r 1]
			}
         }
         "||" {
			set r [posixwaitpid $pid]
			if {[lindex $r 1] == 0} {
               # should not continue because last execution succeeded
               return [lindex $r 1]
			}
         }
         "|" {
			# Nothing to do here
         }
      }
      
      # skipping separator
      incr i
   }
}

proc unknown {args} {
   set margs shell
   lappend margs {*}$args
   bg $margs
}

proc backgrounded_unknown {args} {
   shell {*}$args
}

set parenthesis_list { "(" ")" "{" "}" "[" "]" "<" ">" "\"" "\'" }

proc mgph_remove_trailing_nonalnum {text} {
   global parenthesis_list
   while {![string is alnum [string index $text end]] && [string index $text end] ni $parenthesis_list} {
      set text [string range $text 0 end-1]
   }
   return $text
}

proc mgph_trim_parenthesis {text} {
   global parenthesis_list
   
   set first_char [string index $text 0]
   set last_char [string index $text end]
   
   if {$first_char ni $parenthesis_list || $last_char ni $parenthesis_list} { return $text }
   
   return [string range $text 1 end-1]
}

# examples:
#  include <index.h>
#  bim/bam/bla.c:
#  bim/bam/bla.c:7,32: bang
#  bim/bam/bla.c(2:2): sprac
#  bim/bam/bla.c:[2,3]: sproc
#  bim/bam/bla.c[2:3]
#  bim/bam/bla.c:2: bloc
#  bim/bam/bla.c(2)
#  bim/bam/bla.c:[2]
# go.c

proc mouse_go_preprocessing_hook {text} {
   set text [mgph_remove_trailing_nonalnum $text]
   set text [mgph_trim_parenthesis $text]
   
   puts "Text is <$text>\n"
   
   if {[regexp {^([^:]*)(?::|(?::?[\(\[]))([[:digit:]]+)[,:]([[:digit:]]+)(?:[\]\)])?$} $text -> filename lineno colno]} {
       return "$filename:$lineno:$colno"
   }
   
   if {[regexp {^([^:]*)(?::|(?::?[\(\[]))([[:digit:]]+)(?:[\]\)])?$} $text -> filename lineno]} {
       return "$filename:$lineno:0"
   }
   
   return $text
}

proc bindent {direction indentchar} {
	mark lines
	
	set stored_mark [mark get]
	set stored_cursor [cursor]
	set text [c]
	
	switch -exact $direction {
		"incr" {
			set nt [string map [list "\n" "\n$indentchar"] $text]
			c "$indentchar$nt"
		}
		"decr" {
			set nt [string map [list "\n " "\n" "\n\t" "\n" ] $text]
			if {[string index $nt 0] eq " " || [string index $nt 0] eq "\t"} {
				set nt [string range $nt 1 end]
			}
			c $nt
		}
	}
	
	puts "Stored mark: $stored_mark"
	puts "Stored cursor: $stored_cursor"
	
	go $stored_mark
	mark
	go $stored_cursor
}
