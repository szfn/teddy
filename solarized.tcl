proc macro {} {
	c "set "
	move next line
	go :1
}

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


setcfg editor_bg_color $base03
setcfg border_color $base0
setcfg editor_bg_cursorline $base02

setcfg editor_fg_color $base2

setcfg posbox_border_color 0
setcfg posbox_bg_color 15654274
setcfg posbox_fg_color 0

lexycfg nothing $base2
lexycfg keyword $green
lexycfg comment $base01
lexycfg string $cyan
lexycfg id $base2
lexycfg literal $cyan