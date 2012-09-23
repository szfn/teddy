#ifndef __BUILTIN_TCL__
#define __BUILTIN_TCL__

#define BUILTIN_TCL_CODE "# builtin commands for teddy\n\
\n\
proc kill_line {} {\n\
   m +0:1 +1:1\n\
   if {[undo tag] eq \"kill_line\"} {\n\
   	undo fusenext\n\
   }\n\
   c \"\"\n\
   undo tag kill_line\n\
   cb put [undo get before]\n\
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
         if {$target eq \"\"} { error \"Output redirect without filename\" }\n\
         set fd [fdopen -wronly -trunc -creat $target]\n\
         fddup2 $fd $redirected_descriptor\n\
         fdclose $fd\n\
      }\n\
      \">&\" {\n\
         if {$redirected_descriptor eq \"\"} {set redirected_descriptor 1}\n\
         fddup2 $target $redirected_descriptor\n\
      }\n\
      \"<\" {\n\
         if {$redirected_descriptor eq \"\"} {set redirected_descriptor 0}\n\
         if {$target eq \"\"} { error \"Input redirect without filename\" }\n\
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
         set fd [fdopen -creat -wronly -append $target]\n\
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
   if {[string index [lindex $args 0] 0] eq \"|\"} {\n\
      lset args 0 [string range [lindex $args 0] 1 end]\n\
      | {*}$args\n\
   } else {\n\
      # normal unknown code\n\
      set margs shell\n\
      lappend margs {*}$args\n\
      bg $margs\n\
   }\n\
}\n\
\n\
proc backgrounded_unknown {args} {\n\
   shell {*}$args\n\
}\n\
\n\
proc | {args} {\n\
   global backgrounded\n\
   if {$backgrounded} {\n\
      error \"shellpipe called on a backgrounded interpreter\"\n\
   }\n\
\n\
   set text [c]\n\
\n\
   set pipe [fdpipe]\n\
   set outpipe [fdpipe]\n\
   set errpipe [fdpipe]\n\
   set pid [posixfork]\n\
\n\
   if {$pid < 0} {\n\
      error \"fork failed in shellpipe command\"\n\
   }\n\
\n\
   if {$pid == 0} {\n\
      # new default standard input is pipe's input side\n\
      fdclose [lindex $pipe 1]\n\
      fddup2 [lindex $pipe 0] 0\n\
      fdclose [lindex $pipe 0]\n\
\n\
      # new default standard output is outpipe's output side\n\
      fdclose [lindex $outpipe 0]\n\
      fddup2 [lindex $outpipe 1] 1\n\
      fdclose [lindex $outpipe 1]\n\
\n\
      # new default standard error is errpipe's output side\n\
      fdclose [lindex $errpipe 0]\n\
      fddup2 [lindex $errpipe 1] 2\n\
      fdclose [lindex $errpipe 1]\n\
\n\
      bg -setup\n\
\n\
      posixexit [shell [lindex $args 0] {*}[lrange $args 1 end]]\n\
   } else {\n\
      fdclose [lindex $pipe 0]\n\
      fdclose [lindex $outpipe 1]\n\
      fdclose [lindex $errpipe 1]\n\
\n\
      set sub_input [fd2channel [lindex $pipe 1] write]\n\
      set sub_output [fd2channel [lindex $outpipe 0] read]\n\
      set sub_error [fd2channel [lindex $errpipe 0] read]\n\
\n\
      puts $sub_input $text\n\
      close $sub_input\n\
\n\
      set replacement [read $sub_output]\n\
      set error_text [read $sub_error]\n\
      set r [posixwaitpid $pid]\n\
      close $sub_output\n\
\n\
      if {[lindex $r 1] == 0} {\n\
          c $replacement\n\
      } else {\n\
          error $error_text\n\
      }\n\
   }\n\
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
proc bindent {direction indentchar} {\n\
	set saved_status [m]\n\
\n\
	if {[lindex $saved_status 0] == \"nil\"} {\n\
		m +0:+0 +0:+0\n\
	}\n\
\n\
	buffer select-mode lines\n\
\n\
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
	m {*}$saved_status\n\
	buffer select-mode normal\n\
}\n\
\n\
proc man {args} {\n\
	bg \"+man/$args+\" \"shell man $args\"\n\
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
		{#\\s*(?:include|ifdef|ifndef|if|else|endif|pragma|define)\\>} keyword\n\
\n\
		\"-?(?:0x)[0-9a-fA-F]*\" literal\n\
		\"-?[0-9][0-9]*(?:\\\\.[0-9]+)?(?:e-[0-9]+?)?\" literal\n\
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
		{\\<(?:after|error|lappend|platform|tcl_findLibrary|append|eval|lassign|platform::shell|tcl_startOfNextWord|apply|exec|lindex|proc|tcl_startOfPreviousWord|array|exit|linsert|puts|tcl_wordBreakAfter|auto_execok|expr	list|pwd|tcl_wordBreakBefore|auto_import|fblocked|llength|re_syntax|tcltest|auto_load|fconfigure|load|read|tclvars|auto_mkindex|fcopy|lrange|refchan|tell|auto_mkindex_old|file|lrepeat|regexp|time|auto_qualify|fileevent|lreplace|registry|tm|auto_reset|filename|lreverse|regsub|trace|bgerror|flush|lsearch|rename|unknown|binary|for|lset|return|unload|break|foreach|lsort||unset|catch|format|mathfunc|scan|update|cd|gets|mathop|seek|uplevel|chan|glob|memory|set|upvar|clock|global|msgcat|socket|variable|close|history|namespace|source|vwait|concat|http|open|split|while|continue|if|else|package|string|dde|incr|parray|subst|dict|info|pid|switch|encoding|interp|pkg::create|eof|join|pkg_mkIndex|tcl_endOfWord)\\>} keyword\n\
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
		\"-?(?:0[bB])?[0-9][0-9]*(?:\\\\.[0-9]+)?(?:e-[0-9]+?)?[LljJ]?\" literal\n\
		\"-?(?:0[xX])[0-9a-fA-F]*\" literal\n\
		\"\\<None|True|False\\>\" literal\n\
\n\
		{\\<[a-zA-Z_][a-zA-Z0-9_]*\\>} id\n\
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
		{\\<(?:abstract|continue|for|new|switch|assert|default|goto|package|synchronized|boolean|do|if|private|this|break|double|implements|protected|throw|byte|else|import|public|throws|case|enum|instanceof|return|transient|catch|extends|int|short|try|char|final|interface|static|void|class|finally|long|strictfp|volatile|const|float|native|super|while)\\>} keyword\n\
\n\
		\"-?(?:0x)[0-9a-fA-F]*\" literal\n\
		\"-?[0-9][0-9]*(?:\\\\.[0-9]+)?(?:e-[0-9]+?)?\" literal\n\
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
		\"-?(?:0x)[0-9a-fA-F]*\" literal\n\
		\"-?[0-9][0-9]*(?:\\\\.[0-9]+)?(?:e-[0-9]+?)?\" literal\n\
		{(?:nil|true|false|iota)} literal\n\
\n\
		{\\<[a-zA-Z_][a-zA-Z0-9_]*\\>} id\n\
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
\n\
lexydef filesearch 0 {\n\
		{([^:[:space:]()]+):(\\d+)(?::(\\d+))?} file,1,2,3\n\
		{\\<File \"(.+?)\", line (\\d+)} file,1,2\n\
		{\\<at (\\S+) line (\\d+)} file,1,2\n\
		{\\<in (\\S+) on line (\\d+)} file,1,2\n\
		{([^:[:space:]()]+):\\[(\\d+),(\\d+)\\]} file,1,2,3\n\
		{\\<([^:[:space:]()]+\\.[^:[:space:]()]+)\\>} file\n\
		\".\" nothing\n\
	}\n\
\n\
lexyassoc filesearch {^\\+bg}\n\
lexyassoc filesearch {/$}\n\
\n\
lexydef-create mansearch teddy_intl::man_link_open 0\n\
lexydef mansearch 0 {\n\
		{\\<(\\S+)\\((\\d+)\\)} file,1,2\n\
		\".\" nothing\n\
	}\n\
\n\
lexyassoc mansearch {^\\+man}\n\
\n\
lexydef-create tagsearch teddy_intl::tags_link_open 0\n\
lexydef tagsearch 0 {\n\
		{(\\S+)\\t/(.+)/$} file,1,2\n\
	}\n\
\n\
lexyassoc tagsearch {^\\+tags}\n\
\n\
proc clear {} {\n\
	m 1:1 $:$\n\
	c \"\"\n\
}\n\
\n\
proc buffer_setup_hook {buffer-name} { }\n\
proc buffer_save_hook {} {}\n\
\n\
proc loadhistory {} {\n\
	if [info exists ::env(XDG_CONFIG_HOME)] {\n\
		set histf $::env(XDG_CONFIG_HOME)/teddy/teddy_history\n\
	} else {\n\
		set histf $::env(HOME)/.config/teddy/teddy_history\n\
	}\n\
\n\
	if {[catch {set f [open $histf RDONLY]} err]} {\n\
		puts \"No history\"\n\
		return\n\
	}\n\
\n\
	while {[gets $f line] >= 0} {\n\
		set line [split $line \"\\t\"]\n\
		teddyhistory cmd add [lindex $line 0] [lindex $line 1] [lindex $line 2]\n\
	}\n\
	close $f\n\
}\n\
\n\
proc antique_theme {} {\n\
	setcfg -global editor_bg_color [rgbcolor \"antique white\"]\n\
	setcfg -global border_color [rgbcolor black]\n\
\n\
	setcfg -global editor_fg_color [rgbcolor black]\n\
	setcfg -global posbox_border_color 0\n\
	setcfg -global posbox_bg_color 15654274\n\
	setcfg -global posbox_fg_color 0\n\
\n\
	setcfg -global lexy_nothing [rgbcolor black]\n\
	setcfg -global lexy_keyword [rgbcolor \"midnight blue\"]\n\
	setcfg -global lexy_comment [rgbcolor \"dark green\"]\n\
	setcfg -global lexy_string [rgbcolor \"saddle brown\"]\n\
	setcfg -global lexy_id [rgbcolor black]\n\
	setcfg -global lexy_literal [rgbcolor \"saddle brown\"]\n\
	setcfg -global lexy_file [rgbcolor \"midnight blue\"]\n\
}\n\
\n\
proc zenburn_theme {} {\n\
	setcfg -global editor_bg_color [rgbcolor 12 12 12]\n\
	setcfg -global border_color [rgbcolor white]\n\
	setcfg -global editor_bg_cursorline [rgbcolor 31 31 31]\n\
\n\
	setcfg -global editor_fg_color [rgbcolor white]\n\
\n\
	setcfg -global posbox_border_color 0\n\
	setcfg -global posbox_bg_color 15654274\n\
	setcfg -global posbox_fg_color 0\n\
\n\
	setcfg -global lexy_nothing [rgbcolor white]\n\
	setcfg -global lexy_keyword [rgbcolor 240 223 175]\n\
	setcfg -global lexy_comment [rgbcolor 127 159 127]\n\
	setcfg -global lexy_string [rgbcolor 204 147 147]\n\
	setcfg -global lexy_id [rgbcolor 197 197 183 ]\n\
	setcfg -global lexy_literal [rgbcolor 220 163 163]\n\
}\n\
\n\
proc solarized_theme {} {\n\
	set base03    [rgbcolor 0  43  54]\n\
	set base02    [rgbcolor 7  54  66]\n\
	set base01    [rgbcolor 88 110 117]\n\
	set base00    [rgbcolor 101 123 131]\n\
	set base0     [rgbcolor 131 148 150]\n\
	set base1     [rgbcolor 147 161 161]\n\
	set base2     [rgbcolor 238 232 213]\n\
	set base3 [rgbcolor 253 246 227]\n\
	set yellow    [rgbcolor 181 137   0]\n\
	set orange    [rgbcolor 203  75  22]\n\
	set red       [rgbcolor 220  50  47]\n\
	set magenta   [rgbcolor 211  54 130]\n\
	set violet    [rgbcolor 108 113 196]\n\
	set blue      [rgbcolor 38 139 210]\n\
	set cyan      [rgbcolor 42 161 152]\n\
	set green [rgbcolor 133 153   0]\n\
\n\
\n\
	setcfg -global editor_bg_color $base03\n\
	setcfg -global border_color $base0\n\
	setcfg -global editor_bg_cursorline $base02\n\
\n\
	setcfg -global editor_fg_color $base2\n\
\n\
	setcfg -global posbox_border_color 0\n\
	setcfg -global posbox_bg_color 15654274\n\
	setcfg -global posbox_fg_color 0\n\
\n\
	setcfg -global lexy_nothing $base2\n\
	setcfg -global lexy_keyword $green\n\
	setcfg -global lexy_comment $base01\n\
	setcfg -global lexy_string $cyan\n\
	setcfg -global lexy_id $base2\n\
	setcfg -global lexy_literal $cyan\n\
}\n\
\n\
namespace eval teddy_intl {\n\
	namespace export iopen_search\n\
	proc iopen_search {z} {\n\
		set k [s -literal -get $z]\n\
		#puts \"Searching <$z> -> <$k>\"\n\
		if {[lindex $k 0] ne \"nil\"} {\n\
			m nil [lindex $k 0]\n\
		}\n\
	}\n\
\n\
	namespace export link_open\n\
	proc link_open {islink text} {\n\
		if {$islink} {\n\
			set r [lexy-token 0 $text]\n\
		} else {\n\
			set r [list nothing $text \"\" \"\"]\n\
		}\n\
\n\
		set b [buffer open [lindex $r 1]]\n\
\n\
		if {$b eq \"\"} { return }\n\
\n\
		set line [lindex $r 2]\n\
		set col [lindex $r 3]\n\
\n\
		if {$line eq \"\"} { set line 1 }\n\
		if {$col eq \"\"} { set col 1 }\n\
\n\
		buffer eval $b { m $line:$col }\n\
\n\
		buffer focus $b\n\
	}\n\
\n\
	namespace export man_link_open\n\
	proc man_link_open {islink text} {\n\
		if {!$islink} { return }\n\
\n\
		set r [lexy-token 0 $text]\n\
		if {[lindex $r 2] eq \"\"} { return }\n\
\n\
		man [lindex $r 2] [lindex $r 1]\n\
	}\n\
\n\
	namespace export tags_search_menu_command\n\
	proc tags_search_menu_command {text} {\n\
		set tagout [teddy::tags $text]\n\
		if {[llength $tagout] > 1} {\n\
			set b [buffer make +tags/$text]\n\
			buffer eval $b {\n\
				foreach x $tagout {\n\
					c \"$x\\n\"\n\
				}\n\
			}\n\
		} else {\n\
			tags_link_open 0 [lindex $tagout 0]\n\
		}\n\
	}\n\
\n\
	namespace export tags_link_open\n\
	proc tags_link_open {islink text} {\n\
		set r [lexy-token tagsearch/0 $text]\n\
\n\
		set b [buffer open [lindex $r 1]]\n\
\n\
		if {[lindex $r 2] ne \"\"} {\n\
			buffer eval $b {\n\
				m nil 1:1\n\
				m {*}[s -literal [lindex $r 2]]\n\
			}\n\
		}\n\
\n\
		buffer focus $b\n\
	}\n\
\n\
	namespace export dir\n\
	proc dir {directory} {\n\
	 	if {[string index $directory end] != \"/\"} {\n\
	 		set directory \"$directory/\"\n\
	 	}\n\
		set b [buffer make $directory]\n\
		#buffer eval $b { c \"CIAO!\" }\n\
		bg $b { shell ls {*}$teddy::ls_options $directory }\n\
	}\n\
}\n\
\n\
namespace eval teddy {\n\
	# options passed to ls to display a directory\n\
	namespace export ls_options\n\
	set ls_options {-F -1 --group-directories-first}\n\
\n\
	# returns current line number\n\
	namespace export lineof\n\
	proc lineof {x} {\n\
		return [lindex [split $x \":\"] 0]\n\
	}\n\
\n\
	# reads a file from disk\n\
	namespace export slurp\n\
	proc slurp {path} {\n\
		set thefile [open $path]\n\
		fconfigure $thefile -translation binary\n\
		set r [read $thefile]\n\
		close $thefile\n\
		return $r\n\
	}\n\
\n\
	# deletes irrelevant spaces from the end of lines, and empty lines from the end of file\n\
	namespace export spaceman\n\
	proc spaceman {} {\n\
		set saved [m]\n\
		# delete empty spaces from the end of lines\n\
		m nil 1:1\n\
		s {\\s+$} c \"\"\n\
\n\
		# delete empty lines from the end of files\n\
		m nil 1:1\n\
		s {^.+$} { set last_nonempty_line [m] }\n\
		m [lindex $last_nonempty_line 1] $:$\n\
		c \"\"\n\
\n\
		m {*}$saved\n\
	}\n\
}\n\
\n\
proc forlines {args} {\n\
	if {[llength $args] == 1} {\n\
		set pattern {^.*$}\n\
		set body [lindex $args 0]\n\
	} elseif {[llength $args] == 2} {\n\
		set pattern [lindex $args 0]\n\
		set body [lindex $args 1]\n\
	} else {\n\
		error \"Wrong number of arguments to forlines [llength $args] expected 1 or 2\"\n\
	}\n\
\n\
	m all\n\
	s $pattern {\n\
		m line\n\
		uplevel 1 $body\n\
	}\n\
}\n\
\n\
proc ss {args} {\n\
	buffer eval temp {\n\
		c [lindex $args 0]\n\
		m 1:1\n\
		s {*}[lrange $args 1 end]\n\
		m all\n\
		set text [c]\n\
	}\n\
	return $text\n\
}\n\
"
#endif
