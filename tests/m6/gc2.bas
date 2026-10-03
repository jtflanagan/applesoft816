10 DIM A$(20),B$(5,6),C$(3)
20 FOR R = 1 TO 25
30 FOR I = 0 TO 20: A$(I) = STR$ (R * 100 + I) + "A": NEXT
40 FOR I = 0 TO 5: FOR J = 0 TO 6: B$(I,J) = STR$ (I * 10 + J) + LEFT$ ("BBBBBBBBBB",R / 3 + 1): NEXT : NEXT
50 FOR I = 0 TO 3: C$(I) = "C" + STR$ (R + I): NEXT
60 NEXT R
70 X = FRE (0)
80 PRINT A$(0);" ";A$(20);" ";B$(0,0);" ";B$(5,6);" ";B$(3,4);" ";C$(0);" ";C$(3)
