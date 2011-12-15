#ifndef __BUILTIN_TCL__
#define __BUILTIN_TCL__

#define BUILTIN_TCL_CODE "proc kill_line {} {\n\
   go :1\n\
   mark\n\
   move next line\n\
   cb cut\n\
}\n\
\n\
proc shell_perform_redirection {redirection} {\n\
   set redirected_descriptor [dict get $redirection redirected_descriptor]\n\
   set open_direction [dict get $redirection open_direction]\n\
   set target [dict get $redirection target]\n\
      \n\
   switch -exact $open_direction {\n\
      \">\" {\n\
         if {$redirected_descriptor eq \"\"} {set redirected_descriptor 1}\n\
         #puts \"Sending $redirected_descriptor to $target\"\n\
         set fd [fdopen -wronly $target]\n\
         fddup2 $fd $redirected_descriptor\n\
         fdclose $fd\n\
      }\n\
      \">&\" {\n\
         if {$redirected_descriptor eq \"\"} {set redirected_descriptor 1}\n\
         fddup2 $target $redirected_descriptor\n\
      }\n\
      \"<\" {\n\
         if {$redirected_descriptor eq \"\"} {set redirected_descriptor 0}\n\
         set fd [fdopen -rdonly $target]\n\
         fddup2 $fd $redirected_descriptor\n\
         fdclose $fd\n\
      }\n\
      \"<&\" {\n\
         if {$redirected_descriptor eq \"\"} {set redirected_descriptor 0}\n\
         fddup2 $target $redirected_descriptor\n\
      }\n\
      \">>\" {\n\
         if {$redirected_descriptor eq \"\"} {set redirected_descriptor 1}\n\
         set fd [fdopen -wronly -append $target]\n\
         fddup2 $fd $redirected_descriptor\n\
         fdclose $fd\n\
      }\n\
   }\n\
}\n\
\n\
proc shell_eat {args i specialVarName normalVarName} {\n\
   set special {}\n\
   set normal {}\n\
   \n\
   for {} {$i < [llength $args]} {incr i} {\n\
      set cur [lindex $args $i]\n\
      if {$cur eq \"&&\"} { break }\n\
      if {$cur eq \"||\"} { break }\n\
      if {$cur eq \"|\"} { break }\n\
      set isspecial [regexp {^([0-9]*)(>|>&|<&|<|>>)(.*)$} $cur -> redirected_descriptor open_direction target]\n\
      if {$isspecial} {\n\
         lappend special [dict create redirected_descriptor $redirected_descriptor open_direction $open_direction target $target]\n\
      } else {\n\
         if {[string first * $cur] >= 0} {\n\
			# Perform autoglobbing\n\
			lappend normal {*}[glob $cur]\n\
         } else {\n\
			lappend normal $cur\n\
         }\n\
      }\n\
   }\n\
   \n\
   upvar $specialVarName specialVar\n\
   upvar $normalVarName normalVar\n\
   set specialVar $special\n\
   set normalVar $normal\n\
      \n\
   return $i\n\
}\n\
\n\
proc shell_child_code {special normal pipe} {\n\
   # child code, make redirects and exec\n\
   \n\
   #puts \"message from child code\"\n\
   \n\
   if {$pipe ne \"\"} {\n\
      # default ouput of this process will be pipe's output side\n\
      fdclose [lindex $pipe 0]\n\
      fddup2 [lindex $pipe 1] 1\n\
      fdclose [lindex $pipe 1]\n\
   }\n\
   \n\
   for {set i 0} {$i < [llength $special]} {incr i} {\n\
      shell_perform_redirection [lindex $special $i]\n\
   }\n\
   \n\
   #puts \"Executing $normal\"\n\
   posixexec {*}$normal\n\
   \n\
   posixexit -1\n\
}\n\
\n\
proc shell {args} {\n\
   global backgrounded\n\
   if {!$backgrounded} {\n\
      error \"shell called on a non backgrounded interpreter\"\n\
   }\n\
\n\
   set i 0\n\
 \n\
   while {$i < [llength $args]} {\n\
      set i [shell_eat $args $i special normal]\n\
\n\
      set pipe \"\"\n\
      if {$i < [llength $args]} {\n\
         if {[lindex $args $i] eq \"|\"} {\n\
			set pipe [fdpipe]\n\
         }\n\
      }\n\
\n\
      set pid [posixfork]\n\
      if {$pid < 0} {\n\
         error \"fork failed in 'shell'\"\n\
      }\n\
\n\
      if {$pid == 0} {\n\
         shell_child_code $special $normal $pipe\n\
      }\n\
\n\
      # parent code, wait and exit\n\
\n\
      if {$pipe ne \"\"} {\n\
         # new default standard input is pipe's input side\n\
         fdclose [lindex $pipe 1]\n\
         fddup2 [lindex $pipe 0] 0\n\
         fdclose [lindex $pipe 0]\n\
      }\n\
\n\
      if {$i >= [llength $args]} {\n\
         #puts \"waiting for $pid\"\n\
         set r [posixwaitpid $pid]\n\
         #puts \"wait ended <$r>\"\n\
         return [lindex $r 1]\n\
      }\n\
\n\
      switch -exact [lindex $args $i] {\n\
         \"&&\" {\n\
			#puts \"Processing AND $pid\"\n\
			set r [posixwaitpid $pid]\n\
			#puts \"Child returned [lindex $r 1]\"\n\
			if {[lindex $r 1] != 0} {\n\
               # can not continue because last execution failed\n\
               return [lindex $r 1]\n\
			}\n\
         }\n\
         \"||\" {\n\
			set r [posixwaitpid $pid]\n\
			if {[lindex $r 1] == 0} {\n\
               # should not continue because last execution succeeded\n\
               return [lindex $r 1]\n\
			}\n\
         }\n\
         \"|\" {\n\
			# Nothing to do here\n\
         }\n\
      }\n\
\n\
      # skipping separator\n\
      incr i\n\
   }\n\
}\n\
\n\
proc unknown {args} {\n\
   set margs shell\n\
   lappend margs {*}$args\n\
   bg $margs\n\
}\n\
\n\
proc backgrounded_unknown {args} {\n\
   shell {*}$args\n\
}\n\
\n\
set parenthesis_list { \"(\" \")\" \"{\" \"}\" \"[\" \"]\" \"<\" \">\" \"\\\"\" \"\\'\" }\n\
\n\
proc mgph_remove_trailing_nonalnum {text} {\n\
   global parenthesis_list\n\
   while {![string is alnum [string index $text end]] && [string index $text end] ni $parenthesis_list} {\n\
      set text [string range $text 0 end-1]\n\
   }\n\
   return $text\n\
}\n\
\n\
proc mgph_trim_parenthesis {text} {\n\
   global parenthesis_list\n\
\n\
   set first_char [string index $text 0]\n\
   set last_char [string index $text end]\n\
\n\
   if {$first_char ni $parenthesis_list || $last_char ni $parenthesis_list} { return $text }\n\
\n\
   return [string range $text 1 end-1]\n\
}\n\
\n\
# examples:\n\
#  include <index.h>\n\
#  bim/bam/bla.c:\n\
#  bim/bam/bla.c:7,32: bang\n\
#  bim/bam/bla.c(2:2): sprac\n\
#  bim/bam/bla.c:[2,3]: sproc\n\
#  bim/bam/bla.c[2:3]\n\
#  bim/bam/bla.c:2: bloc\n\
#  bim/bam/bla.c(2)\n\
#  bim/bam/bla.c:[2]\n\
# go.c\n\
\n\
proc mouse_go_preprocessing_hook {text} {\n\
   set text [mgph_remove_trailing_nonalnum $text]\n\
   set text [mgph_trim_parenthesis $text]\n\
\n\
   #puts \"Text is <$text>\\n\"\n\
\n\
   if {[regexp {^([^:]*)(?::|(?::?[\\(\\[]))([[:digit:]]+)[,:]([[:digit:]]+)(?:[\\]\\)])?$} $text -> filename lineno colno]} {\n\
       return \"$filename:$lineno:$colno\"\n\
   }\n\
\n\
   if {[regexp {^([^:]*)(?::|(?::?[\\(\\[]))([[:digit:]]+)(?:[\\]\\)])?$} $text -> filename lineno]} {\n\
       return \"$filename:$lineno:0\"\n\
   }\n\
\n\
   return $text\n\
}\n\
\n\
proc bindent {direction indentchar} {\n\
	mark lines\n\
\n\
	set stored_mark [mark get]\n\
	set stored_cursor [cursor]\n\
	set text [c]\n\
\n\
	switch -exact $direction {\n\
		\"incr\" {\n\
			set nt [string map [list \"\\n\" \"\\n$indentchar\"] $text]\n\
			c \"$indentchar$nt\"\n\
		}\n\
		\"decr\" {\n\
			set nt [string map [list \"\\n \" \"\\n\" \"\\n\\t\" \"\\n\" ] $text]\n\
			if {[string index $nt 0] eq \" \" || [string index $nt 0] eq \"\\t\"} {\n\
				set nt [string range $nt 1 end]\n\
			}\n\
			c $nt\n\
		}\n\
	}\n\
\n\
	#puts \"Stored mark: $stored_mark\"\n\
	#puts \"Stored cursor: $stored_cursor\"\n\
\n\
	go $stored_mark\n\
	mark\n\
	go $stored_cursor\n\
}\n\
\n\
proc lexydef {name args} {\n\
	lexydef-create $name\n\
	for {set i 0} {$i < [llength $args]} {set i [expr $i + 2]} {\n\
		set start_state [lindex $args $i]\n\
		set transitions [lindex $args [expr $i + 1]]\n\
		for {set j 0} {$j < [llength $transitions]} {set j [expr $j + 2]} {\n\
			set pattern [lindex $transitions $j]\n\
			set state_and_token_type [split [lindex $transitions [expr $j + 1]] :]\n\
\n\
			if {[llength $state_and_token_type] > 1} {\n\
				set next_state [lindex $state_and_token_type 0]\n\
				set type [lindex $state_and_token_type 1]\n\
			} else {\n\
				set next_state $start_state\n\
				set type [lindex $state_and_token_type 0]\n\
			}\n\
\n\
			lexydef-append $name $start_state $pattern $next_state $type\n\
		}\n\
	}\n\
}\n\
\n\
lexydef c 0 {\n\
		\"auto|_Bool|break|case|char|_Complex|const|continue|default|do|double|else|enum|extern|float|for|goto|if|_Imaginary|inline|int|long|register|restrict|return|short|signed|sizeof|static|struct|switch|typedef|union|unsigned|void|volatile|while\" keyword\n\
\n\
		\"[a-zA-Z_][a-zA-Z0-9_]*\" id\n\
\n\
		\"//.*$\" comment\n\
		\"/\\\\*\" comment:comment\n\
\n\
		\"'.'\" string\n\
		\"'\\\\.'\" string\n\
		\"\\\"\" string:string\n\
\n\
		\".\" nothing\n\
	} comment {\n\
		\"\\\\*/\" 0:comment\n\
		\".\" comment\n\
	} string {\n\
		\"\\\\.\" string\n\
		\"\\\"\" 0:string\n\
		\".\" string\n\
	}\n\
\n\
lexyassoc c \".c$\"\n\
"
#endif
