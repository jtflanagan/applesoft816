# AppleSoft's known bugs

applesoft816 reproduces the ROM bit for bit, bugs included.  Fixing them is
wanted but deferred (decision 2026-10-04).  When fixes come, they come
behind a per-program switch that is off by default, so the ROM comparison
stays the test.  Until then this is the list of bugs worth fixing.

Status: **verified** = seen in the ROM's code or its output, and
reproduced by us; **reported** = named by someone else and not yet checked
here; **gap** = we don't reproduce it yet.

| # | Bug | Status | Notes |
|---|---|---|---|
| 1 | `ONERR GOTO` skips the rest of its line: statements after it on the same line never run | verified | The ROM ends ONERR with REMN; `compile.c` does the same. Fixed by Peersoft. |
| 2 | `A% = -32768` gives ?ILLEGAL QUANTITY (`A% = -32767.5` works) | verified | The ROM's -32768 constant at $E0FE is a byte short: its last byte is the $20 of the next instruction. `fp816` keeps it. Fixed by Peersoft. |
| 3 | `IF A$ THEN` tests FAC's exponent ($9D), which a string expression doesn't set: the result depends on whatever was computed earlier | gap | The ROM's IF ($D9C9) is FRMEVL, then `LDA $9D`.  We compile string IF as unimplemented; a fix would define it (for example, true if non-empty). |
| 4 | INPUT into a string, typed unquoted, stops at a colon: entering `10:10` gives `10` | reported | Ivan Drucker (NuInput, KansasFest); Peersoft fixes it. Our INPUT follows the ROM. |
| 5 | RETURN and POP | reported | Peersoft's manual lists fixes for both, but its text and source give no detail. Look in S-C DocuMentor (below). |
| 6 | An error caught by ONERR leaves the stack as it stood at the error.  A program that keeps erroring inside GOSUB or FOR runs out of stack.  Apple's workaround is `CALL -3288` in the handler | reported | We handle `CALL -3288` (and `CALL 54915`, STKINI).  Still to check: whether our VM runs out after the same number of errors as the ROM. |
| 7 | LIST with a range that stops before the program's end prints an extra blank line | verified | CRDO comes before the range check ($D6DA). Reproduced (`tests/dos/list1.txt`). |
| 8 | DEL with no lines in range still clears the variables and returns to the prompt.  DEL with the range backwards does nothing and the program carries on | verified | $F331: it only returns early when the end line comes before the start. Reproduced (`tests/dos/list1.txt`). |
| 9 | A cold start leaves the last byte of the RND seed ($CD) unset, so the first sequence depends on leftover memory | verified | Our loader sets the seed to the ROM's constant (80 4F C7 52, then 0). |

Already better, with the same output:

- String garbage collection takes time quadratic in the number of strings
  (long FRE pauses).  Our copying collector has the same results without
  the pauses.

## Where to look for more

- Bob Sander-Cederlof's commented disassembly of AppleSoft (S-C DocuMentor,
  txbobsc.com/scsc/scdocumentor/) names bugs routine by routine.  It is
  the source Peersoft cites for its fixes, and the next thing to go
  through for this list.
- Peersoft 1.5.6 (`~/repos/amper-corpus`, sources on its D34Sources disk):
  bugs 1, 2, 4 and 5.
