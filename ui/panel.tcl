# panel.tcl - Tcl/Tk 控制面板（编进 exe 的 1002 RCDATA）
# 所有键盘快捷键都在这里有对应控件；键盘只是快捷方式。

if {![info exists ::panelW]} { set ::panelW 364; set ::panelH 460; set ::panelX 740; set ::panelY 340 }
wm title . "Control Panel"
wm resizable . 1 1
wm minsize . 364 420
wm geometry . "${::panelW}x${::panelH}+${::panelX}+${::panelY}"
catch {ttk::style theme use clam}
ttk::style configure TButton -padding {6 2}
ttk::style configure TCheckbutton -padding {1 1}
ttk::style configure TCombobox -padding {2 1}

font create DpTitle -family "Segoe UI" -size 11 -weight bold
font create DpHead  -family "Segoe UI" -size 8 -weight bold
font create DpBody  -family "Segoe UI" -size 9
font create DpSmall -family "Segoe UI" -size 8
font create DpMono  -family "Consolas" -size 9

set ::busy 0
set ::last ""
set ::vGrid 1
set ::vPeriodic 0
set ::vCircle 1
set ::s(last) ""

ttk::frame .f -padding {10 8 10 8}
pack .f -fill both -expand 1
grid columnconfigure .f 0 -weight 1

ttk::label .f.title -text "Double Pendulum Phase Space" -font DpTitle -anchor w
grid .f.title -row 0 -column 0 -sticky ew
ttk::label .f.sub -text "phase space · pendulum · hot logic" -font DpSmall -foreground "#666" -anchor w
grid .f.sub -row 1 -column 0 -sticky ew -pady {0 4}
ttk::separator .f.sep0 -orient horizontal
grid .f.sep0 -row 2 -column 0 -sticky ew -pady 2

ttk::frame .f.sim
grid .f.sim -row 3 -column 0 -sticky ew -pady 2
ttk::button .f.sim.pause -text "Pause" -command {act pause} -takefocus 0
ttk::button .f.sim.reset -text "Reset" -command {act reset} -takefocus 0
ttk::button .f.sim.quit -text "Quit" -command {dp_quit} -takefocus 0
grid .f.sim.pause -row 0 -column 0 -sticky ew -padx {0 3}
grid .f.sim.reset -row 0 -column 1 -sticky ew -padx 3
grid .f.sim.quit -row 0 -column 2 -sticky ew -padx {3 0}
grid columnconfigure .f.sim 0 -weight 1
grid columnconfigure .f.sim 1 -weight 1
grid columnconfigure .f.sim 2 -weight 1

ttk::frame .f.comp
grid .f.comp -row 4 -column 0 -sticky ew -pady 4
ttk::label .f.comp.kl -text "Kernel" -font DpBody -anchor w
ttk::combobox .f.comp.kc -state readonly -values {"GPU compute" "Fortran CPU"} -width 16 -takefocus 0
ttk::label .f.comp.pl -text "Palette" -font DpBody -anchor w
ttk::combobox .f.comp.pc -state readonly -values {phase ember ice mono} -width 16 -takefocus 0
grid .f.comp.kl -row 0 -column 0 -sticky w -pady 1
grid .f.comp.kc -row 0 -column 1 -sticky ew -pady 1
grid .f.comp.pl -row 1 -column 0 -sticky w -pady 1
grid .f.comp.pc -row 1 -column 1 -sticky ew -pady 1
grid columnconfigure .f.comp 1 -weight 1
bind .f.comp.kc <<ComboboxSelected>> {act kernel_set [expr {[.f.comp.kc current] == 0 ? "gpu" : "cpu"}]}
bind .f.comp.pc <<ComboboxSelected>> {act palette_set [lindex {phase ember ice mono} [.f.comp.pc current]]}

ttk::separator .f.sep1 -orient horizontal
grid .f.sep1 -row 5 -column 0 -sticky ew -pady 2

ttk::label .f.vh -text "View" -font DpHead -foreground "#555" -anchor w
grid .f.vh -row 6 -column 0 -sticky ew -pady {2 0}
ttk::frame .f.view
grid .f.view -row 7 -column 0 -sticky ew -pady 1
ttk::checkbutton .f.view.g -text "Grid" -variable ::vGrid -command {act grid $::vGrid} -takefocus 0
ttk::checkbutton .f.view.p -text "Periodic" -variable ::vPeriodic -command {act periodic $::vPeriodic} -takefocus 0
ttk::checkbutton .f.view.c -text "Circle" -variable ::vCircle -command {act circle $::vCircle} -takefocus 0
grid .f.view.g -row 0 -column 0 -sticky w
grid .f.view.p -row 0 -column 1 -sticky w -padx 6
grid .f.view.c -row 0 -column 2 -sticky w
ttk::button .f.view.reset -text "Reset view" -command {act view_reset} -takefocus 0
grid .f.view.reset -row 1 -column 0 -columnspan 3 -sticky w -pady {3 0}

ttk::separator .f.sep2 -orient horizontal
grid .f.sep2 -row 8 -column 0 -sticky ew -pady 2

ttk::label .f.hh -text "Hot logic (Lua)" -font DpHead -foreground "#555" -anchor w
grid .f.hh -row 9 -column 0 -sticky ew -pady {2 0}
ttk::label .f.hot_src -text "Source: embedded" -font DpSmall -foreground "#333" -anchor w -justify left -wraplength 330
grid .f.hot_src -row 10 -column 0 -sticky ew
ttk::frame .f.hot_row
grid .f.hot_row -row 11 -column 0 -sticky ew -pady 2
ttk::button .f.hot_row.reload -text "Reload hot.lua (F5)" -command {act reload} -takefocus 0
grid .f.hot_row.reload -row 0 -column 0 -sticky w
ttk::label .f.hot_row.hint -text "hot.lua next to exe -> Reload" -font DpSmall -foreground "#666" -wraplength 150 -justify left
grid .f.hot_row.hint -row 0 -column 1 -sticky w -padx 6

ttk::separator .f.sep3 -orient horizontal
grid .f.sep3 -row 12 -column 0 -sticky ew -pady 2

ttk::frame .f.stat
grid .f.stat -row 13 -column 0 -sticky ew
ttk::label .f.stat.line1 -text "GPU compute | 0 fps | 0x0" -font DpMono -anchor w
ttk::label .f.stat.line2 -text "Selected: none    Zoom: 1.00x" -font DpMono -anchor w
ttk::label .f.stat.line3 -text "Palette: phase" -font DpMono -anchor w
grid .f.stat.line1 -row 0 -column 0 -sticky ew
grid .f.stat.line2 -row 1 -column 0 -sticky ew
grid .f.stat.line3 -row 2 -column 0 -sticky ew

ttk::separator .f.sep4 -orient horizontal
grid .f.sep4 -row 14 -column 0 -sticky ew -pady 2

ttk::label .f.short -text "R reset   Space pause   C palette   K kernel\nF5 reload   G grid   P periodic   O circle   0 view   Esc quit" -font DpSmall -foreground "#555" -justify left -anchor w
grid .f.short -row 15 -column 0 -sticky ew

ttk::label .f.last -text "Last: ready" -font DpSmall -foreground "#333" -anchor w -wraplength 330 -justify left
grid .f.last -row 16 -column 0 -sticky ew -pady {2 2}

proc update_ui {} {
    if {$::busy} return
    set ::busy 1
    array unset ::s
    array set ::s [dp_status]
    .f.sim.pause configure -text [expr {$::s(paused) ? "Resume" : "Pause"}]
    .f.comp.kc current [expr {$::s(kernel) eq "gpu" ? 0 : 1}]
    set pi [lsearch -exact {phase ember ice mono} $::s(scheme)]
    if {$pi >= 0} { .f.comp.pc current $pi }
    set ::vGrid $::s(grid)
    set ::vPeriodic $::s(periodic)
    set ::vCircle $::s(circle)
    .f.stat.line1 configure -text "$::s(kernel_label)  |  $::s(fps) fps  |  $::s(res)x$::s(res)"
    .f.stat.line2 configure -text "Selected: $::s(sel)    Zoom: $::s(zoom)x"
    .f.stat.line3 configure -text "Palette: $::s(scheme)"
    .f.hot_src configure -text "Source: $::s(source)"
    if {$::s(last) ne ""} { set ::last $::s(last) }
    .f.last configure -text "Last: $::last"
    set ::busy 0
}

proc refresh {} {
    update_ui
    after 250 refresh
}

proc act {name {value ""}} {
    set msg [dp_action $name $value]
    if {$msg ne ""} { set ::last $msg }
    focus .
    update_ui
}

foreach k {r R} { bind . <KeyPress-$k> {act reset} }
bind . <space> {act pause}
foreach k {c C} { bind . <KeyPress-$k> {act palette} }
foreach k {k K} { bind . <KeyPress-$k> {act kernel} }
foreach k {g G} { bind . <KeyPress-$k> {act grid} }
foreach k {p P} { bind . <KeyPress-$k> {act periodic} }
foreach k {o O} { bind . <KeyPress-$k> {act circle} }
bind . <KeyPress-0> {act view_reset}
bind . <F5> {act reload}
bind . <Escape> {dp_quit}

wm protocol . WM_DELETE_WINDOW {dp_quit}
wm deiconify .
focus -force .
after 100 refresh