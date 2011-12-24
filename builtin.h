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
\n\
      if {$cur eq \"&&\"} { break }\n\
      if {$cur eq \"||\"} { break }\n\
      if {$cur eq \"|\"} { break }\n\
\n\
      set isspecial [regexp {^([0-9]*)(>|>&|<&|<|>>)(.*)$} $cur -> redirected_descriptor open_direction target]\n\
\n\
      if {$isspecial} {\n\
         lappend special [dict create redirected_descriptor $redirected_descriptor open_direction $open_direction target $target]\n\
      } elseif {[string first ! $cur] == 0} {\n\
      	lappend normal [string range $cur 1 end]\n\
      } else {\n\
         if {[string first ~ $cur] == 0} {\n\
         	global env\n\
         	set cur $env(HOME)/[string range $cur 1 end]\n\
         }\n\
\n\
         if {[string first * $cur] >= 0} {\n\
			# Perform autoglobbing\n\
			if [catch {lappend normal {*}[glob $cur]}] {\n\
				lappend normal $cur\n\
			}\n\
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
		\"\\\\<(?:auto|_Bool|break|case|char|_Complex|const|continue|default|do|double|else|enum|extern|float|for|goto|if|_Imaginary|inline|int|long|register|restrict|return|short|signed|sizeof|static|struct|switch|typedef|union|unsigned|void|volatile|while|int8_t|uint8_t|int16_t|uint16_t|int32_t|uint32_t|int64_t|uint64_t|size_t|time_t|bool)\\\\>\" keyword\n\
\n\
		\"#(?:include|ifdef|ifndef|if|else|endif|pragma|define)\\\\>\" keyword\n\
\n\
		\"-?(?:0x)?[0-9][0-9]*(?:\\\\.[0-9]+)?(?:e-[0-9]+?)?\" literal\n\
		\"NULL|true|false\" literal\n\
\n\
		\"[a-zA-Z_][a-zA-Z0-9_]*\" id\n\
\n\
		\"//.*$\" comment\n\
		\"/\\\\*\" comment:comment\n\
\n\
		\"'.'\" string\n\
		{'\\\\.'} string\n\
		\"\\\"\" string:string\n\
\n\
		\".\" nothing\n\
	} comment {\n\
		\"\\\\*/\" 0:comment\n\
		\".\" comment\n\
	} string {\n\
		{\\\\.} string\n\
		\"\\\"\" 0:string\n\
		\".\" string\n\
	}\n\
\n\
lexyassoc c {\\.c$}\n\
lexyassoc c {\\.h$}\n\
\n\
lexydef tcl 0 {\n\
		{\\<(?:after|error|lappend|platform|tcl_findLibrary|append|eval|lassign|platform::shell|tcl_startOfNextWord|apply|exec|lindex|proc|tcl_startOfPreviousWord|array|exit|linsert|puts|tcl_wordBreakAfter|auto_execok|expr	list|pwd|tcl_wordBreakBefore|auto_import|fblocked|llength|re_syntax|tcltest|auto_load|fconfigure|load|read|tclvars|auto_mkindex|fcopy|lrange|refchan|tell|auto_mkindex_old|file|lrepeat|regexp|time|auto_qualify|fileevent|lreplace|registry|tm|auto_reset|filename|lreverse|regsub|trace|bgerror|flush|lsearch|rename|unknown|binary|for|lset|return|unload|break|foreach|lsort||unset|catch|format|mathfunc|scan|update|cd|gets|mathop|seek|uplevel|chan|glob|memory|set|upvar|clock|global|msgcat|socket|variable|close|history|namespace|source|vwait|concat|http|open|split|while|continue|if|package|string|dde|incr|parray|subst|dict|info|pid|switch|encoding|interp|pkg::create|eof|join|pkg_mkIndex|tcl_endOfWord)\\>} keyword\n\
\n\
		{\\<$[a-zA-Z_][a-zA-Z0-9_]*\\>} id\n\
		{\"} string:string\n\
		{\\{\\*\\}} keyword\n\
		{#.*$} comment\n\
\n\
		{\\<[a-zA-Z0-9]*\\>} nothing\n\
\n\
		\".\" nothing\n\
	} string {\n\
		{\\\\.} string\n\
		{\"} 0:string\n\
		\".\" string\n\
	}\n\
\n\
lexyassoc tcl {\\.tcl$}\n\
\n\
lexydef python 0 {\n\
		{\\<(?:and|del|from|not|while|as|elif|global|or|with|assert|else|if|pass|yield|break|except|import|print|class|exec|in|raise|continue|finally|is|return|def|for|lambda|try)\\>} keyword\n\
\n\
		\"-?(?:0[xbXB])?[0-9][0-9]*(?:\\\\.[0-9]+)?(?:e-[0-9]+?)?[LljJ]?\" literal\n\
		\"\\<None|True|False\\>\" literal\n\
\n\
		{\\<$[a-zA-Z_][a-zA-Z0-9_]*\\>} id\n\
\n\
		{(?:r|u|ur|R|U|UR|Ur|uR|b|B|br|Br|bR|BR)?\"\"\"} lstringq:string\n\
		{(?:r|u|ur|R|U|UR|Ur|uR|b|B|br|Br|bR|BR)?'''} lstringq:string\n\
		{(?:r|u|ur|R|U|UR|Ur|uR|b|B|br|Br|bR|BR)?\"} stringqq:string\n\
		{(?:r|u|ur|R|U|UR|Ur|uR|b|B|br|Br|bR|BR)?'} stringq:string\n\
\n\
		{#.*$} comment\n\
\n\
		\".\" nothing\n\
	} stringqq {\n\
		{\\\\.} string\n\
		{\"} 0:string\n\
		{.} string\n\
	} stringq {\n\
		{\\\\.} string\n\
		{'} 0:string\n\
		{.} string\n\
	} lstringqq {\n\
		{\\\\.} string\n\
		{\"\"\"} 0:string\n\
		{.} string\n\
	} lstringq {\n\
		{\\\\.} string\n\
		{'''} 0:string\n\
		{.} string\n\
	}\n\
\n\
lexyassoc python {\\.py$}\n\
\n\
lexydef java 0 {\n\
		{\\<(?:abstract|continue|for|new|switch|assert|default|goto|package|synchronized|boolean|do|if|private|this|break|double|implements|protected|throw|byte|else|import|public|throws|case|enum|instanceof|return|transient|catch|extends|int|short|trychar|final|interface|static|void|class|finally|long|strictfp|volatile|const|float|native|super|while)\\>} keyword\n\
\n\
		\"-?(?:0x)?[0-9][0-9]*(?:\\\\.[0-9]+)?(?:e-[0-9]+?)?\" literal\n\
		\"null|true|false\" literal\n\
\n\
		\"[a-zA-Z_][a-zA-Z0-9_]*\" id\n\
\n\
		\"//.*$\" comment\n\
		\"/\\\\*\" comment:comment\n\
\n\
		\"'.'\" string\n\
		{'\\\\.'} string\n\
		\"\\\"\" string:string\n\
\n\
		\".\" nothing\n\
	} comment {\n\
		\"\\\\*/\" 0:comment\n\
		\".\" comment\n\
	} string {\n\
		{\\\\.} string\n\
		\"\\\"\" 0:string\n\
		\".\" string\n\
	}\n\
\n\
lexyassoc java {\\.java$}\n\
\n\
lexydef go 0 {\n\
		{\\<(?:break|default|func|interface|select|case|defer|go|map|struct|chan|else|goto|package|switch|const|fallthrough|if|range|type|continue|for|import|return|var)\\>} keyword\n\
\n\
		\"-?(?:0x)?[0-9][0-9]*(?:\\\\.[0-9]+)?(?:e-[0-9]+?)?\" literal\n\
		{(?:nil|true|false|iota)} literal\n\
\n\
		{\\<$[a-zA-Z_][a-zA-Z0-9_]*\\>} id\n\
\n\
		\"//.*$\" comment\n\
		\"/\\\\*\" comment:comment\n\
\n\
		\"'.'\" string\n\
		{'\\\\.'} string\n\
		\"\\\"\" string:string\n\
\n\
		\".\" nothing\n\
	} comment {\n\
		\"\\\\*/\" 0:comment\n\
		\".\" comment\n\
	} string {\n\
		{\\\\.} string\n\
		\"\\\"\" 0:string\n\
		\".\" string\n\
	}\n\
\n\
lexyassoc go {\\.go$}\n\
"
#endif
