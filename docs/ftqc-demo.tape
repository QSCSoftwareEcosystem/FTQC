Output "$FTQC_ROOT/docs/ftqc-demo.gif"

Set Shell "bash"
Set FontSize 18
Set Width 1200
Set Height 720
Set TypingSpeed 75ms
Set Theme "Catppuccin Mocha"

Hide
Type "cd $FTQC_ROOT"
Enter
Type "source docs/ftqc-demo-helpers.sh"
Enter
Show
Set TypingSpeed 75ms
Sleep 0s
Ctrl+L

Type "show_circuit"
Enter
Sleep 4s
Ctrl+L

Type "logical_mlir"
Enter
Sleep 5s
Ctrl+L

Type "resource_estimate"
Enter
Sleep 5s
Ctrl+L

Type "physical_mlir"
Enter
Sleep 5s
Ctrl+L

Type "stim_lowering"
Enter
Sleep 5s
Ctrl+L

Type "stim_ler"
Enter
Sleep 5s
Ctrl+L

Type "mapped_mlir"
Enter
Sleep 5s
Ctrl+L

Type "export_iqm"
Enter
Sleep 5s
