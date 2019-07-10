
; Zero Page
.include "zp.inc"

; hardware addresses/soft_switches
.include "hardware.inc"

main:
	;===================
	; init screen
	jsr	TEXT
	jsr	HOME
	bit	KEYRESET

	;===================
	; init vars

	lda	#0
	sta	DRAW_PAGE

	lda	#<inst_text
	sta	OUTL
	lda	#>inst_text
	sta	OUTH

	jsr	move_and_print
	jsr	move_and_print

	jsr	wait_until_keypressed		; tail call?

	rts

;	    0         1         2         3
;	    0123456789012345678901234567890123456789
inst_text:

.byte  6, 0,	 "abcdefghijklmnopqrstuvwxyz",0

.byte  7, 3,	  "ABCDEFGHIJKLMNOPQRSTUVWXYZ",0

.include "gr_offsets.s"
.include "keypress.s"
.include "text_print.s"

