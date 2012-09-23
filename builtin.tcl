#### UTILITIES ##################################################################
# Utility commands the user could find interesting

proc kill_line {} {
   m +0:1 +1:1
   if {[undo tag] eq "kill_line"} {
      undo fusenext
   }
   c ""
   undo tag kill_line
   cb put [undo get before]
}

proc bindent {direction indentchar} {
	set saved_status [m]

	if {[lindex $saved_status 0] == "nil"} {
		m +0:+0 +0:+0
	}

	buffer select-mode lines

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

	m {*}$saved_status
	buffer select-mode normal
}

proc man {args} {
	teddy::bg "+man/$args+" "shell man $args"
}

proc clear {} {
	m 1:1 $:$
	c ""
}

proc forlines {args} {
	if {[llength $args] == 1} {
		set pattern {^.*$}
		set body [lindex $args 0]
	} elseif {[llength $args] == 2} {
		set pattern [lindex $args 0]
		set body [lindex $args 1]
	} else {
		error "Wrong number of arguments to forlines [llength $args] expected 1 or 2"
	}

	m all
	s $pattern {
		m line
		uplevel 1 $body
	}
}

proc ss {args} {
	buffer eval temp {
		c [lindex $args 0]
		m 1:1
		s {*}[lrange $args 1 end]
		m all
		set text [c]
	}
	return $text
}

namespace eval teddy {
	# options passed to ls to display a directory
	namespace export ls_options
	set ls_options {-F -1 --group-directories-first}

	# returns current line number
	namespace export lineof
	proc lineof {x} {
		return [lindex [split $x ":"] 0]
	}

	# reads a file from disk
	namespace export slurp
	proc slurp {path} {
		set thefile [open $path]
		fconfigure $thefile -translation binary
		set r [read $thefile]
		close $thefile
		return $r
	}

	# deletes irrelevant spaces from the end of lines, and empty lines from the end of file
	namespace export spaceman
	proc spaceman {} {
		set saved [m]
		# delete empty spaces from the end of lines
		m nil 1:1
		s {\s+$} c ""

		# delete empty lines from the end of files
		set last_nonempty_line "nil"
		s {^.+$} { set last_nonempty_line [m] }
		if {$last_nonempty_line ne "nil"} {
			m [lindex $last_nonempty_line 1] $:$
			c ""
		}

		m {*}$saved
	}
}

#### THEMES ##################################################################
# Color themes for teddy

proc antique_theme {} {
	setcfg -global editor_bg_color [rgbcolor "antique white"]
	setcfg -global border_color [rgbcolor black]

	setcfg -global editor_fg_color [rgbcolor black]
	setcfg -global posbox_border_color 0
	setcfg -global posbox_bg_color 15654274
	setcfg -global posbox_fg_color 0

	setcfg -global lexy_nothing [rgbcolor black]
	setcfg -global lexy_keyword [rgbcolor "midnight blue"]
	setcfg -global lexy_comment [rgbcolor "dark green"]
	setcfg -global lexy_string [rgbcolor "saddle brown"]
	setcfg -global lexy_id [rgbcolor black]
	setcfg -global lexy_literal [rgbcolor "saddle brown"]
	setcfg -global lexy_file [rgbcolor "midnight blue"]
}

proc zenburn_theme {} {
	setcfg -global editor_bg_color [rgbcolor 12 12 12]
	setcfg -global border_color [rgbcolor white]
	setcfg -global editor_bg_cursorline [rgbcolor 31 31 31]

	setcfg -global editor_fg_color [rgbcolor white]

	setcfg -global posbox_border_color 0
	setcfg -global posbox_bg_color 15654274
	setcfg -global posbox_fg_color 0

	setcfg -global lexy_nothing [rgbcolor white]
	setcfg -global lexy_keyword [rgbcolor 240 223 175]
	setcfg -global lexy_comment [rgbcolor 127 159 127]
	setcfg -global lexy_string [rgbcolor 204 147 147]
	setcfg -global lexy_id [rgbcolor 197 197 183 ]
	setcfg -global lexy_literal [rgbcolor 220 163 163]
}

proc solarized_theme {} {
	set base03    [rgbcolor 0  43  54]
	set base02    [rgbcolor 7  54  66]
	set base01    [rgbcolor 88 110 117]
	set base00    [rgbcolor 101 123 131]
	set base0     [rgbcolor 131 148 150]
	set base1     [rgbcolor 147 161 161]
	set base2     [rgbcolor 238 232 213]
	set base3 [rgbcolor 253 246 227]
	set yellow    [rgbcolor 181 137   0]
	set orange    [rgbcolor 203  75  22]
	set red       [rgbcolor 220  50  47]
	set magenta   [rgbcolor 211  54 130]
	set violet    [rgbcolor 108 113 196]
	set blue      [rgbcolor 38 139 210]
	set cyan      [rgbcolor 42 161 152]
	set green [rgbcolor 133 153   0]


	setcfg -global editor_bg_color $base03
	setcfg -global border_color $base0
	setcfg -global editor_bg_cursorline $base02

	setcfg -global editor_fg_color $base2

	setcfg -global posbox_border_color 0
	setcfg -global posbox_bg_color 15654274
	setcfg -global posbox_fg_color 0

	setcfg -global lexy_nothing $base2
	setcfg -global lexy_keyword $green
	setcfg -global lexy_comment $base01
	setcfg -global lexy_string $cyan
	setcfg -global lexy_id $base2
	setcfg -global lexy_literal $cyan
}

### IMPLEMENTATION OF USER COMMANDS #######################################
# Implementations of commands useful to the user

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

proc shell {args} {
   global backgrounded
   if {!$backgrounded} {
      error "shell called on a non backgrounded interpreter"
   }

   set i 0

   while {$i < [llength $args]} {
      set i [teddy_intl::shell_eat $args $i special normal]

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
         teddy_intl::shell_child_code $special $normal $pipe
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

proc shellsync {text args} {
   global backgrounded
   if {$backgrounded} {
      error "shellpipe called on a backgrounded interpreter"
   }

   set pipe [fdpipe]
   set outpipe [fdpipe]
   set errpipe [fdpipe]
   set pid [posixfork]

   if {$pid < 0} {
      error "fork failed in shellpipe command"
   }

   if {$pid == 0} {
      # new default standard input is pipe's input side
      fdclose [lindex $pipe 1]
      fddup2 [lindex $pipe 0] 0
      fdclose [lindex $pipe 0]

      # new default standard output is outpipe's output side
      fdclose [lindex $outpipe 0]
      fddup2 [lindex $outpipe 1] 1
      fdclose [lindex $outpipe 1]

      # new default standard error is errpipe's output side
      fdclose [lindex $errpipe 0]
      fddup2 [lindex $errpipe 1] 2
      fdclose [lindex $errpipe 1]

      teddy::bg -setup

      posixexit [shell [lindex $args 0] {*}[lrange $args 1 end]]
   } else {
      fdclose [lindex $pipe 0]
      fdclose [lindex $outpipe 1]
      fdclose [lindex $errpipe 1]

      set sub_input [fd2channel [lindex $pipe 1] write]
      set sub_output [fd2channel [lindex $outpipe 0] read]
      set sub_error [fd2channel [lindex $errpipe 0] read]

      puts $sub_input $text
      close $sub_input

      set replacement [read $sub_output]
      set error_text [read $sub_error]
      set r [posixwaitpid $pid]
      close $sub_output

      if {[lindex $r 1] == 0} {
          return $replacement
      } else {
          error $error_text
      }
   }
}

proc unknown {args} {
   if {[string index [lindex $args 0] 0] eq "|"} {
      lset args 0 [string range [lindex $args 0] 1 end]
      c [shellsync [c] {*}$args]
   } else {
      # normal unknown code
      set margs shell
      lappend margs {*}$args
      teddy::bg $margs
   }
}

proc backgrounded_unknown {args} {
   shell {*}$args
}

#### INTERNAL COMMANDS #####################################################
# Commands called internally by teddy (teddy_intl namespace)

namespace eval teddy_intl {
	namespace export iopen_search
	proc iopen_search {z} {
		set k [s -literal -get $z]
		#puts "Searching <$z> -> <$k>"
		if {[lindex $k 0] ne "nil"} {
			m nil [lindex $k 0]
		}
	}

	namespace export link_open
	proc link_open {islink text} {
		if {$islink} {
			set r [lexy-token 0 $text]
		} else {
			set r [list nothing $text "" ""]
		}

		set b [buffer open [lindex $r 1]]

		if {$b eq ""} { return }

		set line [lindex $r 2]
		set col [lindex $r 3]

		if {$line ne ""} {
			if {$col eq ""} { set col 1 }
			buffer eval $b { m nil $line:$col }
			buffer focus $b
		}
	}

	namespace export man_link_open
	proc man_link_open {islink text} {
		if {!$islink} { return }

		set r [lexy-token 0 $text]
		if {[lindex $r 2] eq ""} { return }

		man [lindex $r 2] [lindex $r 1]
	}

	namespace export tags_search_menu_command
	proc tags_search_menu_command {text} {
		set tagout [teddy::tags $text]
		if {[llength $tagout] > 1} {
			set b [buffer make +tags/$text]
			buffer eval $b {
				foreach x $tagout {
					c "$x\n"
				}
			}
		} else {
			tags_link_open 0 [lindex $tagout 0]
		}
	}

	namespace export tags_link_open
	proc tags_link_open {islink text} {
		set r [lexy-token tagsearch/0 $text]

		set b [buffer open [lindex $r 1]]

		if {[lindex $r 2] ne ""} {
			buffer eval $b {
				m nil 1:1
				m {*}[s -literal [lindex $r 2]]
			}
		}

		buffer focus $b
	}

	namespace export dir
	proc dir {directory} {
	 	if {[string index $directory end] != "/"} {
	 		set directory "$directory/"
	 	}
		set b [buffer make $directory]
		#buffer eval $b { c "CIAO!" }
		teddy::bg $b { shell ls {*}$teddy::ls_options $directory }
	}

	namespace export loadhistory
	proc loadhistory {} {
		if [info exists ::env(XDG_CONFIG_HOME)] {
			set histf $::env(XDG_CONFIG_HOME)/teddy/teddy_history
		} else {
			set histf $::env(HOME)/.config/teddy/teddy_history
		}

		if {[catch {set f [open $histf RDONLY]} err]} {
			puts "No history"
			return
		}

		while {[gets $f line] >= 0} {
			set line [split $line "\t"]
			teddy::history cmd add [lindex $line 0] [lindex $line 1] [lindex $line 2]
		}
		close $f
	}

	namespace export shell_eat
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

	namespace export shell_perform_redirection
	proc shell_perform_redirection {redirection} {
	   set redirected_descriptor [dict get $redirection redirected_descriptor]
	   set open_direction [dict get $redirection open_direction]
	   set target [dict get $redirection target]

	   switch -exact $open_direction {
	      ">" {
	         if {$redirected_descriptor eq ""} {set redirected_descriptor 1}
	         if {$target eq ""} { error "Output redirect without filename" }
	         set fd [fdopen -wronly -trunc -creat $target]
	         fddup2 $fd $redirected_descriptor
	         fdclose $fd
	      }
	      ">&" {
	         if {$redirected_descriptor eq ""} {set redirected_descriptor 1}
	         fddup2 $target $redirected_descriptor
	      }
	      "<" {
	         if {$redirected_descriptor eq ""} {set redirected_descriptor 0}
	         if {$target eq ""} { error "Input redirect without filename" }
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
	         set fd [fdopen -creat -wronly -append $target]
	         fddup2 $fd $redirected_descriptor
	         fdclose $fd
	      }
	   }
	}

	namespace export shell_child_code
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
	      teddy_intl::shell_perform_redirection [lindex $special $i]
	   }

	   #puts "Executing $normal"
	   posixexec {*}$normal

	   posixexit -1
	}
}

#### DEFAULT HOOKS ###########################################################
# Default buffer hooks (empty)

proc buffer_setup_hook {buffer-name} { }
proc buffer_save_hook {} {}

#### LEXY DEFINITIONS ##########################################################
# Definitions of lexy state machines to syntax highlight source code

lexydef c 0 {
		"\\<(?:auto|_Bool|break|case|char|_Complex|const|continue|default|do|double|else|enum|extern|float|for|goto|if|_Imaginary|inline|int|long|register|restrict|return|short|signed|sizeof|static|struct|switch|typedef|union|unsigned|void|volatile|while|int8_t|uint8_t|int16_t|uint16_t|int32_t|uint32_t|int64_t|uint64_t|size_t|time_t|bool)\\>" keyword

		{#\s*(?:include|ifdef|ifndef|if|else|endif|pragma|define)\>} keyword

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
		{\<(?:after|error|lappend|platform|tcl_findLibrary|append|eval|lassign|platform::shell|tcl_startOfNextWord|apply|exec|lindex|proc|tcl_startOfPreviousWord|array|exit|linsert|puts|tcl_wordBreakAfter|auto_execok|expr	list|pwd|tcl_wordBreakBefore|auto_import|fblocked|llength|re_syntax|tcltest|auto_load|fconfigure|load|read|tclvars|auto_mkindex|fcopy|lrange|refchan|tell|auto_mkindex_old|file|lrepeat|regexp|time|auto_qualify|fileevent|lreplace|registry|tm|auto_reset|filename|lreverse|regsub|trace|bgerror|flush|lsearch|rename|unknown|binary|for|lset|return|unload|break|foreach|lsort||unset|catch|format|mathfunc|scan|update|cd|gets|mathop|seek|uplevel|chan|glob|memory|set|upvar|clock|global|msgcat|socket|variable|close|history|namespace|source|vwait|concat|http|open|split|while|continue|if|else|package|string|dde|incr|parray|subst|dict|info|pid|switch|encoding|interp|pkg::create|eof|join|pkg_mkIndex|tcl_endOfWord)\>} keyword

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

		{\<[a-zA-Z_][a-zA-Z0-9_]*\>} id

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
		{\<(?:abstract|continue|for|new|switch|assert|default|goto|package|synchronized|boolean|do|if|private|this|break|double|implements|protected|throw|byte|else|import|public|throws|case|enum|instanceof|return|transient|catch|extends|int|short|try|char|final|interface|static|void|class|finally|long|strictfp|volatile|const|float|native|super|while)\>} keyword

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

		{\<[a-zA-Z_][a-zA-Z0-9_]*\>} id

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

lexydef filesearch 0 {
		{([^:[:space:]()]+):(\d+)(?::(\d+))?} file,1,2,3
		{\<File "(.+?)", line (\d+)} file,1,2
		{\<at (\S+) line (\d+)} file,1,2
		{\<in (\S+) on line (\d+)} file,1,2
		{([^:[:space:]()]+):\[(\d+),(\d+)\]} file,1,2,3
		{\<([^:[:space:]()]+\.[^:[:space:]()]+)\>} file
		"." nothing
	}

lexyassoc filesearch {^\+bg}
lexyassoc filesearch {/$}

lexydef-create mansearch teddy_intl::man_link_open 0
lexydef mansearch 0 {
		{\<(\S+)\((\d+)\)} file,1,2
		"." nothing
	}

lexyassoc mansearch {^\+man}

lexydef-create tagsearch teddy_intl::tags_link_open 0
lexydef tagsearch 0 {
		{(\S+)\t/(.+)/$} file,1,2
	}

lexyassoc tagsearch {^\+tags}