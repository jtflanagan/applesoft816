10 A$="HELLO":B$=" WORLD":C$=A$+B$:PRINT C$;LEN(C$)
20 PRINT LEFT$(C$,3);"|";RIGHT$(C$,3);"|";MID$(C$,4,4);"|";MID$(C$,7);"|";MID$(C$,20);"|"
30 PRINT LEFT$(C$,99);"|";RIGHT$(C$,99);"|";MID$(C$,1,0);"|"
40 PRINT ASC("A");" ";ASC(C$);" ";CHR$(65);CHR$(66)+CHR$(67);" ";LEN("")
50 PRINT STR$(1/3);"|";STR$(-12.5);"|";STR$(1E20);"|";VAL("123.5")+1;" ";VAL("  -7E2X");" ";VAL("")
60 PRINT "A"<"B";"A"="A";"AB">"A";"A"<"AB";"B"<"AB";""="";"Z">"";"ABC"<>"ABD"
70 IF A$="HELLO" THEN PRINT "EQUAL"
80 IF A$<>"HELLO" THEN PRINT "WRONG"
90 X$="":FOR I=1 TO 10:X$=X$+CHR$(64+I):NEXT:PRINT X$
100 D$="ABC":E$=D$:D$="XYZ":PRINT D$;E$
110 PRINT "["+"]";"["+A$+"]";(A$+B$=C$)
