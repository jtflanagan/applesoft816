10 DIM B$(5,6),C$(3),A$(20)
20 FOR I = 0 TO 3: C$(I) = "KEEP" + STR$ (I): NEXT
30 FOR I = 0 TO 5: FOR J = 0 TO 6: B$(I,J) = STR$ (I * 10 + J) + "B": NEXT : NEXT
40 FOR R = 1 TO 60
50 FOR I = 0 TO 20: A$(I) = STR$ (R * 100 + I) + "-------------------A": NEXT
60 X = FRE (0)
70 NEXT R
80 PRINT A$(0);" ";A$(20);" ";B$(0,0);" ";B$(5,6);" ";B$(3,4);" ";C$(0);" ";C$(3)
