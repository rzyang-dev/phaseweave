# panel.tcl - schema-driven control panel for the experiment atlas
# C exposes: dp_action, dp_status, dp_query, dp_quit.
# Parameters and axes are queried from the experiment; no Lua reload for values.

if {![info exists ::panelW]} { set ::panelW 364; set ::panelH 620; set ::panelX 740; set ::panelY 340 }
wm title . "Control Panel"
wm resizable . 1 1
wm minsize . 364 420
wm geometry . "${::panelW}x${::panelH}+${::panelX}+${::panelY}"
catch {ttk::style theme use clam}
ttk::style configure TButton -padding {6 2}
ttk::style configure TCheckbutton -padding {1 1}
ttk::style configure TCombobox -padding {2 1}
ttk::style configure TNotebook.Tab -padding {8 3}

font create DpTitle -family "Segoe UI" -size 11 -weight bold
font create DpHead  -family "Segoe UI" -size 8 -weight bold
font create DpBody  -family "Segoe UI" -size 9
font create DpSmall -family "Segoe UI" -size 8
font create DpMono  -family "Consolas" -size 9

set ::busy 0
set ::last ""
set ::params {}
set ::channels {}
set ::axes {}
set ::view {}
set ::s(last) ""

ttk::frame .f -padding {8 6 8 6}
pack .f -fill both -expand 1
grid columnconfigure .f 0 -weight 1
grid rowconfigure .f 2 -weight 1

ttk::label .f.title -text "Dynamical Systems Atlas" -font DpTitle -anchor w
grid .f.title -row 0 -column 0 -sticky ew
ttk::frame .f.mrow
grid .f.mrow -row 1 -column 0 -sticky ew -pady {2 2}
ttk::label .f.mrow.l -text "Model" -font DpBody -anchor w
ttk::combobox .f.mrow.cb -state readonly -width 22 -takefocus 0
grid .f.mrow.l -row 0 -column 0 -sticky w
grid .f.mrow.cb -row 0 -column 1 -sticky ew
grid columnconfigure .f.mrow 1 -weight 1
grid columnconfigure .f 0 -weight 1
bind .f.mrow.cb <<ComboboxSelected>> {model_changed}

ttk::notebook .f.nb
grid .f.nb -row 2 -column 0 -sticky nsew -pady {4 2}

# ---------------------------------------------------------------- View tab
ttk::frame .f.nb.view -padding 8
ttk::frame .f.nb.view.actions
grid .f.nb.view.actions -row 0 -column 0 -columnspan 2 -sticky ew
ttk::button .f.nb.view.actions.pause -text "Pause" -command {act pause} -takefocus 0
ttk::button .f.nb.view.actions.reset -text "Reset" -command {act reset} -takefocus 0
ttk::button .f.nb.view.actions.quit -text "Quit" -command {dp_quit} -takefocus 0
grid .f.nb.view.actions.pause -row 0 -column 0 -sticky ew -padx {0 3}
grid .f.nb.view.actions.reset -row 0 -column 1 -sticky ew -padx 3
grid .f.nb.view.actions.quit -row 0 -column 2 -sticky ew -padx {3 0}
grid columnconfigure .f.nb.view.actions 0 -weight 1
grid columnconfigure .f.nb.view.actions 1 -weight 1
grid columnconfigure .f.nb.view.actions 2 -weight 1

ttk::label .f.nb.view.cl -text "Color by" -font DpBody -anchor w
ttk::combobox .f.nb.view.ch -state readonly -width 16 -takefocus 0
ttk::label .f.nb.view.vl -text "View" -font DpBody -anchor w
ttk::combobox .f.nb.view.post -state readonly -values {raw gradient range variance} -width 12 -takefocus 0
ttk::label .f.nb.view.pl -text "Palette" -font DpBody -anchor w
ttk::combobox .f.nb.view.pal -state readonly -values {phase ember ice mono} -width 16 -takefocus 0
grid .f.nb.view.cl -row 1 -column 0 -sticky w -pady 2
grid .f.nb.view.ch -row 1 -column 1 -sticky ew -pady 2
grid .f.nb.view.vl -row 2 -column 0 -sticky w -pady 2
grid .f.nb.view.post -row 2 -column 1 -sticky ew -pady 2
grid .f.nb.view.pl -row 3 -column 0 -sticky w -pady 2
grid .f.nb.view.pal -row 3 -column 1 -sticky ew -pady 2
grid columnconfigure .f.nb.view 1 -weight 1
bind .f.nb.view.ch <<ComboboxSelected>> {act channel_set [.f.nb.view.ch get]}
bind .f.nb.view.post <<ComboboxSelected>> {act post_set [.f.nb.view.post get]}
bind .f.nb.view.pal <<ComboboxSelected>> {act palette_set [.f.nb.view.pal get]}

ttk::label .f.nb.view.kl -text "Kernel" -font DpBody -anchor w
ttk::combobox .f.nb.view.kern -state readonly -values {CPU GPU} -width 12 -takefocus 0
grid .f.nb.view.kl -row 4 -column 0 -sticky w -pady 2
grid .f.nb.view.kern -row 4 -column 1 -sticky ew -pady 2
bind .f.nb.view.kern <<ComboboxSelected>> {act backend_set [string tolower [.f.nb.view.kern get]]}

ttk::frame .f.nb.view.cbar
grid .f.nb.view.cbar -row 5 -column 0 -columnspan 2 -sticky ew -pady {6 0}
ttk::label .f.nb.view.cbar.name -text "color" -font DpSmall -anchor w
ttk::label .f.nb.view.cbar.max -text "" -font DpSmall -anchor e
canvas .f.nb.view.cbar.c -height 12 -highlightthickness 1 -highlightbackground "#999" -background "#ddd"
ttk::label .f.nb.view.cbar.min -text "" -font DpSmall -anchor w
grid .f.nb.view.cbar.name -row 0 -column 0 -sticky w
grid .f.nb.view.cbar.max -row 0 -column 1 -sticky e
grid .f.nb.view.cbar.c -row 1 -column 0 -columnspan 2 -sticky ew
grid .f.nb.view.cbar.min -row 2 -column 0 -sticky w
grid .f.nb.view.cbar.max -row 2 -column 1 -sticky e
grid columnconfigure .f.nb.view.cbar 1 -weight 1
bind .f.nb.view.cbar.c <Configure> {draw_colorbar}

ttk::frame .f.nb.view.checks
grid .f.nb.view.checks -row 6 -column 0 -columnspan 2 -sticky ew -pady {4 0}
ttk::checkbutton .f.nb.view.checks.g -text "Grid" -variable ::vGrid -command {act grid $::vGrid} -takefocus 0
ttk::checkbutton .f.nb.view.checks.p -text "Periodic" -variable ::vPeriodic -command {act periodic $::vPeriodic} -takefocus 0
ttk::checkbutton .f.nb.view.checks.c -text "Circle" -variable ::vCircle -command {act circle $::vCircle} -takefocus 0
grid .f.nb.view.checks.g -row 0 -column 0 -sticky w
grid .f.nb.view.checks.p -row 0 -column 1 -sticky w -padx 6
grid .f.nb.view.checks.c -row 0 -column 2 -sticky w
ttk::button .f.nb.view.rv -text "Reset view" -command {act view_reset} -takefocus 0
grid .f.nb.view.rv -row 7 -column 0 -columnspan 2 -sticky w -pady {4 0}
ttk::frame .f.nb.view.presets
grid .f.nb.view.presets -row 8 -column 0 -columnspan 2 -sticky ew -pady {4 0}
ttk::button .f.nb.view.presets.a -text "Angle map" -command {preset angles} -takefocus 0
ttk::button .f.nb.view.presets.m -text "Mass map" -command {preset masses} -takefocus 0
grid .f.nb.view.presets.a -row 0 -column 0 -sticky ew -padx {0 3}
grid .f.nb.view.presets.m -row 0 -column 1 -sticky ew -padx {3 0}
grid columnconfigure .f.nb.view.presets 0 -weight 1
grid columnconfigure .f.nb.view.presets 1 -weight 1

ttk::frame .f.nb.view.exports
grid .f.nb.view.exports -row 9 -column 0 -columnspan 2 -sticky ew -pady {6 0}
ttk::button .f.nb.view.exports.png -text "Export PNG" -command {act export_png} -takefocus 0
ttk::button .f.nb.view.exports.csv -text "Export CSV" -command {act export_csv} -takefocus 0
grid .f.nb.view.exports.png -row 0 -column 0 -sticky ew -padx {0 3}
grid .f.nb.view.exports.csv -row 0 -column 1 -sticky ew -padx {3 0}
grid columnconfigure .f.nb.view.exports 0 -weight 1
grid columnconfigure .f.nb.view.exports 1 -weight 1

ttk::label .f.nb.view.hint -text "Mouse: drag=pan, wheel=zoom, right-click=pick. Keyboard: R Space C G P O 0 F5 Esc." -font DpSmall -foreground "#555" -wraplength 320 -justify left
grid .f.nb.view.hint -row 10 -column 0 -columnspan 2 -sticky ew -pady {8 0}
ttk::label .f.nb.view.axes -text "" -font DpSmall -foreground "#555" -anchor w -wraplength 330
grid .f.nb.view.axes -row 11 -column 0 -columnspan 2 -sticky ew
ttk::frame .f.nb.insp -padding 8
ttk::label .f.nb.insp.h -text "Selected pixel" -font DpTitle -anchor w
grid .f.nb.insp.h -row 0 -column 0 -sticky ew
ttk::label .f.nb.insp.txt -text "click a pixel to inspect" -font DpMono -foreground "#222" -justify left -anchor nw -wraplength 330
grid .f.nb.insp.txt -row 1 -column 0 -sticky nsew -pady {6 0}
ttk::label .f.nb.insp.hint -text "Right-click the atlas to update this panel." -font DpSmall -foreground "#555" -wraplength 330 -justify left
grid .f.nb.insp.hint -row 2 -column 0 -sticky ew -pady {8 0}
grid rowconfigure .f.nb.insp 1 -weight 1
grid columnconfigure .f.nb.insp 0 -weight 1

# ---------------------------------------------------------- Parameters tab
ttk::frame .f.nb.par -padding 8
ttk::label .f.nb.par.axl -text "X axis" -font DpBody -anchor w
ttk::combobox .f.nb.par.ax -state readonly -width 14 -takefocus 0
ttk::label .f.nb.par.ayl -text "Y axis" -font DpBody -anchor w
ttk::combobox .f.nb.par.ay -state readonly -width 14 -takefocus 0
grid .f.nb.par.axl -row 0 -column 0 -sticky w
grid .f.nb.par.ax -row 0 -column 1 -sticky ew
grid .f.nb.par.ayl -row 1 -column 0 -sticky w
grid .f.nb.par.ay -row 1 -column 1 -sticky ew
grid columnconfigure .f.nb.par 1 -weight 1
bind .f.nb.par.ax <<ComboboxSelected>> {set_axis x}
bind .f.nb.par.ay <<ComboboxSelected>> {set_axis y}

ttk::frame .f.nb.par.ranges
grid .f.nb.par.ranges -row 2 -column 0 -columnspan 2 -sticky ew -pady {4 0}
ttk::label .f.nb.par.ranges.xl -text "X min/max" -font DpSmall -anchor w
ttk::entry .f.nb.par.ranges.x0 -textvariable ::xmin -width 8 -takefocus 0
ttk::entry .f.nb.par.ranges.x1 -textvariable ::xmax -width 8 -takefocus 0
ttk::label .f.nb.par.ranges.yl -text "Y min/max" -font DpSmall -anchor w
ttk::entry .f.nb.par.ranges.y0 -textvariable ::ymin -width 8 -takefocus 0
ttk::entry .f.nb.par.ranges.y1 -textvariable ::ymax -width 8 -takefocus 0
grid .f.nb.par.ranges.xl -row 0 -column 0 -sticky w
grid .f.nb.par.ranges.x0 -row 0 -column 1 -sticky w
grid .f.nb.par.ranges.x1 -row 0 -column 2 -sticky w
grid .f.nb.par.ranges.yl -row 1 -column 0 -sticky w
grid .f.nb.par.ranges.y0 -row 1 -column 1 -sticky w
grid .f.nb.par.ranges.y1 -row 1 -column 2 -sticky w

ttk::frame .f.nb.par.integ
grid .f.nb.par.integ -row 3 -column 0 -columnspan 2 -sticky ew -pady {4 0}
ttk::label .f.nb.par.integ.rl -text "Res" -font DpSmall -anchor w
ttk::entry .f.nb.par.integ.re -textvariable ::res -width 5 -takefocus 0
ttk::label .f.nb.par.integ.dl -text "dt" -font DpSmall -anchor w
ttk::entry .f.nb.par.integ.de -textvariable ::dt -width 6 -takefocus 0
ttk::label .f.nb.par.integ.sl -text "Steps/frame" -font DpSmall -anchor w
ttk::entry .f.nb.par.integ.se -textvariable ::steps -width 4 -takefocus 0
grid .f.nb.par.integ.rl -row 0 -column 0 -sticky w
grid .f.nb.par.integ.re -row 0 -column 1 -sticky w
grid .f.nb.par.integ.dl -row 0 -column 2 -sticky w -padx {6 0}
grid .f.nb.par.integ.de -row 0 -column 3 -sticky w
grid .f.nb.par.integ.sl -row 1 -column 0 -sticky w
grid .f.nb.par.integ.se -row 1 -column 1 -sticky w

ttk::frame .f.nb.par.plist
grid .f.nb.par.plist -row 4 -column 0 -columnspan 2 -sticky nsew -pady {6 0}
grid columnconfigure .f.nb.par.plist 1 -weight 1
grid rowconfigure .f.nb.par 4 -weight 1

ttk::frame .f.nb.par.buttons
grid .f.nb.par.buttons -row 5 -column 0 -columnspan 2 -sticky ew -pady {4 0}
ttk::button .f.nb.par.buttons.apply -text "Apply parameters" -command {apply_params} -takefocus 0
grid .f.nb.par.buttons.apply -row 0 -column 0 -sticky w

# -------------------------------------------------------------- Script tab
ttk::frame .f.nb.script -padding 8
ttk::label .f.nb.script.h -text "Experiment script" -font DpHead -foreground "#555" -anchor w
ttk::label .f.nb.script.src -text "experiments/experiment.lua" -font DpSmall -foreground "#333" -anchor w -justify left -wraplength 320
ttk::button .f.nb.script.reload -text "Reload script (F5)" -command {act reload} -takefocus 0
ttk::label .f.nb.script.hint -text "The model dynamics are built in. This script owns schema metadata and the palette; parameter values still apply from the Parameters tab." -font DpSmall -foreground "#666" -wraplength 320 -justify left
ttk::label .f.nb.script.ch -text "Axes, parameters and channels are defined by the selected model." -font DpSmall -foreground "#555" -wraplength 320 -justify left
grid .f.nb.script.h -row 0 -column 0 -sticky ew
grid .f.nb.script.src -row 1 -column 0 -sticky ew -pady {2 6}
grid .f.nb.script.reload -row 2 -column 0 -sticky w
grid .f.nb.script.hint -row 3 -column 0 -sticky ew -pady {8 0}
grid .f.nb.script.ch -row 4 -column 0 -sticky ew -pady {8 0}

# --------------------------------------------------------------- status bar
ttk::frame .f.stat
grid .f.stat -row 3 -column 0 -sticky ew
ttk::label .f.stat.line1 -text "CPU (C) | 0 fps | 0x0" -font DpMono -anchor w
ttk::label .f.stat.line2 -text "Selected: none | Zoom: 1.00x" -font DpMono -anchor w
ttk::label .f.stat.line3 -text "theta1 | raw" -font DpMono -anchor w
ttk::label .f.stat.last -text "Last: ready" -font DpSmall -foreground "#333" -anchor w -wraplength 330 -justify left
grid .f.stat.line1 -row 0 -column 0 -sticky ew
grid .f.stat.line2 -row 1 -column 0 -sticky ew
grid .f.stat.line3 -row 2 -column 0 -sticky ew
grid .f.stat.last -row 3 -column 0 -sticky ew -pady {2 0}

.f.nb add .f.nb.view -text "View"
.f.nb add .f.nb.par -text "Parameters"
.f.nb add .f.nb.script -text "Script"
.f.nb add .f.nb.insp -text "Inspect"
.f.nb select .f.nb.view

proc call {name {value ""}} {
    set m [dp_action $name $value]
    if {$m ne ""} { set ::last $m }
    return $m
}

proc draw_colorbar {} {
    set c .f.nb.view.cbar.c
    set pal [dp_query palette]
    set n [llength $pal]
    if {$n < 1} { return }
    set w [winfo width $c]
    if {$w < 2} { set w 330 }
    $c delete all
    for {set i 0} {$i < $n} {incr i} {
        set x0 [expr {$w * $i / $n}]
        set x1 [expr {$w * ($i + 1) / $n}]
        $c create rectangle $x0 0 $x1 12 -fill [lindex $pal $i] -outline ""
    }
    set idx [.f.nb.view.ch current]
    if {$idx < 0} { set idx 0 }
    set ch [lindex [dp_query channels] $idx]
    if {$ch eq ""} { return }
    lassign $ch name label unit mn mx cyc
    if {$unit ne ""} { set unit " $unit" }
    .f.nb.view.cbar.name configure -text "$label"
    .f.nb.view.cbar.min configure -text [format "%.4g%s" $mn $unit]
    .f.nb.view.cbar.max configure -text [format "%.4g%s" $mx $unit]
}

proc update_ui {} {
    if {$::busy} return
    set ::busy 1
    array unset ::s
    array set ::s [dp_status]
    .f.nb.view.actions.pause configure -text [expr {$::s(paused) ? "Resume" : "Pause"}]
    set ::vGrid $::s(grid)
    set ::vPeriodic $::s(periodic)
    set ::vCircle $::s(circle)
    .f.stat.line1 configure -text "$::s(kernel_label)  |  $::s(fps) fps  |  $::s(res)x$::s(res)"
    .f.stat.line2 configure -text "Selected: $::s(sel)  |  Zoom: $::s(zoom)x"
    if {[info exists ::s(kernel)]} {
        .f.nb.view.kern set [expr {$::s(kernel) eq "gpu" ? "GPU" : "CPU"}]
    }
    if {[catch {set v [dp_query view]}] == 0} {
        lassign $v ch post pal
        .f.nb.view.ch set $ch
        .f.nb.view.post set $post
        .f.nb.view.pal set $pal
        .f.stat.line3 configure -text "$ch  |  $post"
        catch {draw_colorbar}
    }
    if {[catch {lassign [dp_query axes] ax ay xmn xmx ymn ymx}] == 0} {
        .f.nb.view.axes configure -text [format {X %s [%.4g, %.4g]    Y %s [%.4g, %.4g]} $ax $xmn $xmx $ay $ymn $ymx]
    }
    if {[catch {set seltext [dp_query selected]}] == 0} {
        .f.nb.insp.txt configure -text $seltext
    }
    if {$::s(last) ne ""} { set ::last $::s(last) }
    .f.stat.last configure -text "Last: $::last"
    set ::busy 0
}

proc model_changed {} {
    set idx [.f.mrow.cb current]
    if {$idx < 0 || $idx >= [llength $::model_ids]} { return }
    call model_set [lindex $::model_ids $idx]
    rebuild_schema
    update_ui
}

proc rebuild_schema {} {
    set ::model_ids {}
    set ::model_labels {}
    foreach m [dp_query models] {
        lassign $m id title
        lappend ::model_ids $id
        lappend ::model_labels $title
    }
    .f.mrow.cb configure -values $::model_labels
    if {[catch {set cur [dp_query model]}] == 0} {
        lassign $cur mid mtitle
        set mi [lsearch -exact $::model_ids $mid]
        if {$mi >= 0} { .f.mrow.cb current $mi }
    }
    set ::params [dp_query params]
    set ::axes [dp_query axes]
    set ::channels [dp_query channels]
    set names {}
    foreach p $::params {
        if {[lindex $p 6]} { lappend names [lindex $p 0] }
    }
    .f.nb.par.ax configure -values $names
    .f.nb.par.ay configure -values $names
    if {[catch {set ig [dp_query integration]}] == 0} {
        lassign $ig res dt steps
        set ::res $res; set ::dt $dt; set ::steps $steps
    }
    lassign $::axes ax ay xmin xmax ymin ymax
    .f.nb.par.ax set $ax
    .f.nb.par.ay set $ay
    set ::xmin $xmin; set ::xmax $xmax; set ::ymin $ymin; set ::ymax $ymax
    set chnames {}
    foreach c $::channels { lappend chnames [lindex $c 0] }
    .f.nb.view.ch configure -values $chnames
    foreach w [winfo children .f.nb.par.plist] { destroy $w }
    array unset ::pv
    set r 0
    set col 0
    set idx 0
    foreach p $::params {
        lassign $p name label value pmin pmax step axis group
        set ::pv($name) $value
        ttk::label .f.nb.par.plist.l$idx -text $label -font DpBody -anchor w
        ttk::entry .f.nb.par.plist.e$idx -textvariable ::pv($name) -width 9 -takefocus 0
        grid .f.nb.par.plist.l$idx -row $r -column [expr {$col * 2}] -sticky w -pady 1
        grid .f.nb.par.plist.e$idx -row $r -column [expr {$col * 2 + 1}] -sticky ew -pady 1 -padx {0 6}
        incr idx
        incr col
        if {$col >= 2} { set col 0; incr r }
    }
    grid columnconfigure .f.nb.par.plist 1 -weight 1
    grid columnconfigure .f.nb.par.plist 3 -weight 1
    update_ui
}

proc apply_params {} {
    foreach p $::params {
        set name [lindex $p 0]
        if {[catch {set v [expr {double($::pv($name))}]}]} {
            set ::last "$name: invalid number"
            return
        }
        call param_set "$name=$v"
    }
    if {[catch {set a [expr {double($::xmin)}]}] || [catch {set b [expr {double($::xmax)}]} ||
        [catch {set c [expr {double($::ymin)}]}] || [catch {set d [expr {double($::ymax)}]}]} {
        set ::last "range: invalid number"
        return
    }
    call range_set "x=$a,$b"
    call range_set "y=$c,$d"
    if {[catch {set rv [expr {int($::res)}]}] == 0} { call res_set $rv }
    if {[catch {set dv [expr {double($::dt)}]}] == 0} { call dt_set $dv }
    if {[catch {set sv [expr {int($::steps)}]}] == 0} { call steps_set $sv }
    call apply
    update_ui
}

proc set_axis {which} {
    if {$which eq "x"} { set name [.f.nb.par.ax get] } else { set name [.f.nb.par.ay get] }
    call axis_set "$which=$name"
    rebuild_schema
}

proc preset {which} {
    call preset $which
    rebuild_schema
    update_ui
}

proc act {name {value ""}} {
    set m [call $name $value]
    if {$m ne ""} { set ::last $m }
    if {$name eq "reload" || $name eq "model_set"} { rebuild_schema }
    focus .
    update_ui
}

proc refresh {} {
    update_ui
    after 250 refresh
}

foreach k {r R} { bind . <KeyPress-$k> {act reset} }
bind . <space> {act pause}
foreach k {c C} { bind . <KeyPress-$k> {act palette} }
foreach k {g G} { bind . <KeyPress-$k> {act grid} }
foreach k {p P} { bind . <KeyPress-$k> {act periodic} }
foreach k {o O} { bind . <KeyPress-$k> {act circle} }
bind . <KeyPress-0> {act view_reset}
bind . <F5> {act reload}
bind . <Escape> {dp_quit}

wm protocol . WM_DELETE_WINDOW {dp_quit}
wm deiconify .
focus -force .
after 100 { rebuild_schema; refresh }