# builtin commands for teddy

proc kill_line {} {
   go :1
   mark transient
   mark lines
   move next line
   go :1
   if {[undo tag] eq "kill_line"} {
   	undo fusenext
   }
   c ""
   undo tag kill_line
   cb pput [undo get before]
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
      } elseif {[string first ! $cur] == 0} {
      	lappend normal [string range $cur 1 end]
      } else {
         if {[string first ~ $cur] == 0} {
         	global env
         	set cur $env(HOME)/[string range $cur 1 end]
         }

         if {[string first * $cur] >= 0} {
			# Perform autoglobbing
			if [catch {lappend normal {*}[glob $cur]}] {
				lappend normal $cur
			}
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

   #puts "Text is <$text>"

   if {[regexp {^([^:]*)(?::|(?::?[\(\[]))([[:digit:]]+)[,:]([[:digit:]]+)(?:[\]\)])?$} $text -> filename lineno colno]} {
       #puts "Returning <$filename:$lineno:$colno>"
       return "$filename:$lineno:$colno"
   }

   if {[regexp {^([^:]*)(?::|(?::?[\(\[]))([[:digit:]]+)(?:[\]\)])?$} $text -> filename lineno]} {
       #puts "Returning <$filename:$lineno:0>"
       return "$filename:$lineno:0"
   }

   #puts "Returning <$text>\n"

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

	#puts "Stored mark: $stored_mark"
	#puts "Stored cursor: $stored_cursor"

	go $stored_mark
	mark
	go $stored_cursor
}

proc man {args} {
	bg "+man/$args+" "shell man $args"
}

proc bufman_exp {} {
	set bufman [buffer make "+bufman+"]

	buffer propset $bufman bufman-previous-buffer [buffer current]

	go -here $bufman

	foreach buf [buffer ls] {
		set bufinfo [buffer info $buf]

		c [dict get $bufinfo id]
		c "\t"
		c [dict get $bufinfo name]
		c "\t"
		c [dict get $bufinfo path]
		c "\n"
	}

	buffer setkeyprocessor $bufman bufman_keyprocessor
}

proc lexydef {name args} {
	lexydef-create $name
	for {set i 0} {$i < [llength $args]} {set i [expr $i + 2]} {
		set start_state [lindex $args $i]
		set transitions [lindex $args [expr $i + 1]]
		for {set j 0} {$j < [llength $transitions]} {set j [expr $j + 2]} {
			set pattern [lindex $transitions $j]
			set state_and_token_type [split [lindex $transitions [expr $j + 1]] :]

			if {[llength $state_and_token_type] > 1} {
				set next_state [lindex $state_and_token_type 0]
				set type [lindex $state_and_token_type 1]
			} else {
				set next_state $start_state
				set type [lindex $state_and_token_type 0]
			}

			lexydef-append $name $start_state $pattern $next_state $type
		}
	}
}

lexydef c 0 {
		"\\<(?:auto|_Bool|break|case|char|_Complex|const|continue|default|do|double|else|enum|extern|float|for|goto|if|_Imaginary|inline|int|long|register|restrict|return|short|signed|sizeof|static|struct|switch|typedef|union|unsigned|void|volatile|while|int8_t|uint8_t|int16_t|uint16_t|int32_t|uint32_t|int64_t|uint64_t|size_t|time_t|bool)\\>" keyword

		"#(?:include|ifdef|ifndef|if|else|endif|pragma|define)\\>" keyword

		"-?(?:0x)[0-9a-fA-F]*" literal
		"-?[0-9][0-9]*(?:\\.[0-9]+)?(?:e-[0-9]+?)?" literal
		"NULL|true|false" literal

		"[a-zA-Z_][a-zA-Z0-9_]*" id

		"//.*$" comment
		"/\\*" comment:comment

		"'.'" string
		{'\\.'} string
		"\"" string:string

		"." nothing
	} comment {
		"\\*/" 0:comment
		"." comment
	} string {
		{\\.} string
		"\"" 0:string
		"." string
	}

lexyassoc c {\.c$}
lexyassoc c {\.h$}

lexydef tcl 0 {
		{\<(?:after|error|lappend|platform|tcl_findLibrary|append|eval|lassign|platform::shell|tcl_startOfNextWord|apply|exec|lindex|proc|tcl_startOfPreviousWord|array|exit|linsert|puts|tcl_wordBreakAfter|auto_execok|expr	list|pwd|tcl_wordBreakBefore|auto_import|fblocked|llength|re_syntax|tcltest|auto_load|fconfigure|load|read|tclvars|auto_mkindex|fcopy|lrange|refchan|tell|auto_mkindex_old|file|lrepeat|regexp|time|auto_qualify|fileevent|lreplace|registry|tm|auto_reset|filename|lreverse|regsub|trace|bgerror|flush|lsearch|rename|unknown|binary|for|lset|return|unload|break|foreach|lsort||unset|catch|format|mathfunc|scan|update|cd|gets|mathop|seek|uplevel|chan|glob|memory|set|upvar|clock|global|msgcat|socket|variable|close|history|namespace|source|vwait|concat|http|open|split|while|continue|if|package|string|dde|incr|parray|subst|dict|info|pid|switch|encoding|interp|pkg::create|eof|join|pkg_mkIndex|tcl_endOfWord)\>} keyword

		{\<$[a-zA-Z_][a-zA-Z0-9_]*\>} id
		{"} string:string
		{\{\*\}} keyword
		{#.*$} comment

		{\<[a-zA-Z0-9]*\>} nothing

		"." nothing
	} string {
		{\\.} string
		{"} 0:string
		"." string
	}

lexyassoc tcl {\.tcl$}

lexydef python 0 {
		{\<(?:and|del|from|not|while|as|elif|global|or|with|assert|else|if|pass|yield|break|except|import|print|class|exec|in|raise|continue|finally|is|return|def|for|lambda|try)\>} keyword

		"-?(?:0[bB])?[0-9][0-9]*(?:\\.[0-9]+)?(?:e-[0-9]+?)?[LljJ]?" literal
		"-?(?:0[xX])[0-9a-fA-F]*" literal
		"\<None|True|False\>" literal

		{\<$[a-zA-Z_][a-zA-Z0-9_]*\>} id

		{(?:r|u|ur|R|U|UR|Ur|uR|b|B|br|Br|bR|BR)?"""} lstringq:string
		{(?:r|u|ur|R|U|UR|Ur|uR|b|B|br|Br|bR|BR)?'''} lstringq:string
		{(?:r|u|ur|R|U|UR|Ur|uR|b|B|br|Br|bR|BR)?"} stringqq:string
		{(?:r|u|ur|R|U|UR|Ur|uR|b|B|br|Br|bR|BR)?'} stringq:string

		{#.*$} comment

		"." nothing
	} stringqq {
		{\\.} string
		{"} 0:string
		{.} string
	} stringq {
		{\\.} string
		{'} 0:string
		{.} string
	} lstringqq {
		{\\.} string
		{"""} 0:string
		{.} string
	} lstringq {
		{\\.} string
		{'''} 0:string
		{.} string
	}

lexyassoc python {\.py$}

lexydef java 0 {
		{\<(?:abstract|continue|for|new|switch|assert|default|goto|package|synchronized|boolean|do|if|private|this|break|double|implements|protected|throw|byte|else|import|public|throws|case|enum|instanceof|return|transient|catch|extends|int|short|trychar|final|interface|static|void|class|finally|long|strictfp|volatile|const|float|native|super|while)\>} keyword

		"-?(?:0x)[0-9a-fA-F]*" literal
		"-?[0-9][0-9]*(?:\\.[0-9]+)?(?:e-[0-9]+?)?" literal
		"null|true|false" literal

		"[a-zA-Z_][a-zA-Z0-9_]*" id

		"//.*$" comment
		"/\\*" comment:comment

		"'.'" string
		{'\\.'} string
		"\"" string:string

		"." nothing
	} comment {
		"\\*/" 0:comment
		"." comment
	} string {
		{\\.} string
		"\"" 0:string
		"." string
	}

lexyassoc java {\.java$}

lexydef go 0 {
		{\<(?:break|default|func|interface|select|case|defer|go|map|struct|chan|else|goto|package|switch|const|fallthrough|if|range|type|continue|for|import|return|var)\>} keyword

		"-?(?:0x)[0-9a-fA-F]*" literal
		"-?[0-9][0-9]*(?:\\.[0-9]+)?(?:e-[0-9]+?)?" literal
		{(?:nil|true|false|iota)} literal

		{\<$[a-zA-Z_][a-zA-Z0-9_]*\>} id

		"//.*$" comment
		"/\\*" comment:comment

		"'.'" string
		{'\\.'} string
		"\"" string:string

		"." nothing
	} comment {
		"\\*/" 0:comment
		"." comment
	} string {
		{\\.} string
		"\"" 0:string
		"." string
	}

lexyassoc go {\.go$}
