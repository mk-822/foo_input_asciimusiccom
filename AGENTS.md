# Project workflow

- After implementing and testing a source-code fix, run
  `powershell -ExecutionPolicy Bypass -File tools\build.ps1 -Configuration Release`.
- Confirm that `dist\foo_input_asciimusiccom.fb2k-component` was regenerated
  successfully, and include its location in the completion report.
