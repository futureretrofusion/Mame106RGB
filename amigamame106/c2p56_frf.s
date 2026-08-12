;
; FRF_NATIVE_C2P56_4PLUS2_V5
;
; Dedicated native 5-plane and EHB 4+2 chunky-to-planar converters.
;
; Based on the merge stages used by Mikael Kalms' c2p1x1_8_c5_bm,
; already bundled with Mame106MiniMix. This entry point is deliberately
; fixed to depth 5 or 6 and converts all active planes for each 32-pixel
; block before advancing.
;
; Register ABI matches the bundled _c2p:
;
; d0.w  width in chunky pixels (must be a multiple of 32)
; d1.w  height in pixels
; d2.w  destination X in pixels (must be a multiple of 8)
; d3.w  destination Y in pixels
; d4.l  chunky bytes per row
; a0    chunky source
; a1    struct BitMap *
;
; Exports:
;   _frf_c2p5  exact five-plane converter
;   _frf_c2p6  exact six-plane converter
;
; The routine is non-reentrant, like the original bundled converter.
;

        XDEF    _frf_c2p5
        XDEF    _frf_c2p6

        incdir  include:
        include graphics/gfx.i

        section code,code

_frf_c2p5:
        movem.l d2-d7/a2-a6,-(sp)
        cmpi.b  #5,bm_Depth(a1)
        bne     frf_c2p56_exit
        move.b  #5,frf_c2p_depth
        bra     frf_c2p56_common

_frf_c2p6:
        movem.l d2-d7/a2-a6,-(sp)
        cmpi.b  #6,bm_Depth(a1)
        bne     frf_c2p56_exit
        move.b  #6,frf_c2p_depth

frf_c2p56_common:
        tst.w   d1
        beq     frf_c2p56_exit
        tst.w   d0
        beq     frf_c2p56_exit

        move.w  d0,d5
        andi.w  #$001f,d5
        bne     frf_c2p56_exit

        move.w  d2,d5
        andi.w  #$0007,d5
        bne     frf_c2p56_exit

        move.w  d1,frf_c2p_rows

        moveq   #0,d5
        move.w  d0,d5
        move.l  d5,frf_c2p_width

        move.l  d4,d6
        sub.l   d5,d6
        move.l  d6,frf_c2p_srcmod

        moveq   #0,d6
        move.w  bm_BytesPerRow(a1),d6

        move.l  d5,d7
        lsr.l   #3,d7

        move.l  d6,d5
        sub.l   d7,d5
        move.l  d5,frf_c2p_rowmod

        moveq   #0,d7
        move.w  d3,d7
        mulu.w  d6,d7

        moveq   #0,d5
        move.w  d2,d5
        lsr.l   #3,d5
        add.l   d5,d7

        movem.l bm_Planes(a1),a3-a6

        cmpi.b  #6,frf_c2p_depth
        bne     .load_plane4
        move.l  bm_Planes+20(a1),a2

.load_plane4:
        move.l  bm_Planes+16(a1),a1

        add.l   d7,a3
        add.l   d7,a4
        add.l   d7,a5
        add.l   d7,a6
        add.l   d7,a1

        cmpi.b  #6,frf_c2p_depth
        bne     .row
        add.l   d7,a2

.row:
        move.l  a0,d0
        add.l   frf_c2p_width(pc),d0
        move.l  d0,frf_c2p_rowend

.block:
        move.l  (a0)+,d0
        move.l  (a0)+,d2
        move.l  (a0)+,d1
        move.l  (a0)+,d3

        move.l  #$0f0f0f0f,d6
        and.l   d6,d0
        and.l   d6,d1
        and.l   d6,d2
        and.l   d6,d3
        lsl.l   #4,d0
        lsl.l   #4,d1
        or.l    d2,d0
        or.l    d3,d1

        move.l  (a0)+,d2
        move.l  (a0)+,d6
        move.l  (a0)+,d3
        move.l  (a0)+,d7

        move.l  #$0f0f0f0f,d4
        and.l   d4,d2
        and.l   d4,d6
        and.l   d4,d3
        and.l   d4,d7
        lsl.l   #4,d2
        lsl.l   #4,d3
        or.l    d6,d2
        or.l    d7,d3

        move.w  d2,d6
        move.w  d3,d7
        move.w  d0,d2
        move.w  d1,d3
        swap    d2
        swap    d3
        move.w  d2,d0
        move.w  d3,d1
        move.w  d6,d2
        move.w  d7,d3

        move.l  #$33333333,d4
        move.l  d2,d6
        move.l  d3,d7
        lsr.l   #2,d6
        lsr.l   #2,d7
        eor.l   d0,d6
        eor.l   d1,d7
        and.l   d4,d6
        and.l   d4,d7
        eor.l   d6,d0
        eor.l   d7,d1
        lsl.l   #2,d6
        lsl.l   #2,d7
        eor.l   d6,d2
        eor.l   d7,d3

        move.l  #$00ff00ff,d4
        move.l  d1,d6
        move.l  d3,d7
        lsr.l   #8,d6
        lsr.l   #8,d7
        eor.l   d0,d6
        eor.l   d2,d7
        and.l   d4,d6
        and.l   d4,d7
        eor.l   d6,d0
        eor.l   d7,d2
        lsl.l   #8,d6
        lsl.l   #8,d7
        eor.l   d6,d1
        eor.l   d7,d3

        move.l  #$55555555,d4
        move.l  d1,d5
        move.l  d3,d7
        lsr.l   #1,d5
        lsr.l   #1,d7
        eor.l   d0,d5
        eor.l   d2,d7
        and.l   d4,d5
        and.l   d4,d7
        eor.l   d5,d0
        eor.l   d7,d2
        add.l   d5,d5
        add.l   d7,d7
        eor.l   d1,d5
        eor.l   d3,d7

        move.l  d7,-(sp)
        move.l  d2,-(sp)
        move.l  d5,-(sp)
        move.l  d0,-(sp)

        suba.w  #32,a0

        move.l  (a0)+,d0
        move.l  (a0)+,d2
        move.l  (a0)+,d1
        move.l  (a0)+,d3

        move.l  #$f0f0f0f0,d6
        and.l   d6,d0
        and.l   d6,d1
        and.l   d6,d2
        and.l   d6,d3
        lsr.l   #4,d2
        lsr.l   #4,d3
        or.l    d2,d0
        or.l    d3,d1

        move.l  (a0)+,d2
        move.l  (a0)+,d6
        move.l  (a0)+,d3
        move.l  (a0)+,d7

        move.l  #$f0f0f0f0,d4
        and.l   d4,d2
        and.l   d4,d6
        and.l   d4,d3
        and.l   d4,d7
        lsr.l   #4,d6
        lsr.l   #4,d7
        or.l    d6,d2
        or.l    d7,d3

        move.w  d2,d6
        move.w  d3,d7
        move.w  d0,d2
        move.w  d1,d3
        swap    d2
        swap    d3
        move.w  d2,d0
        move.w  d3,d1
        move.w  d6,d2
        move.w  d7,d3

        ; FRF_NATIVE_C2P56_4PLUS2_FASTPATH_V5
        ; Continue only the data paths producing planes 4 and 5.
        ; The generic upper-nibble transpose also computes planes 6 and 7,
        ; which neither 32-colour mode nor EHB can display.

        move.l  #$33333333,d4
        move.l  d2,d6
        move.l  d3,d7
        lsr.l   #2,d6
        lsr.l   #2,d7
        eor.l   d0,d6
        eor.l   d1,d7
        and.l   d4,d6
        and.l   d4,d7
        lsl.l   #2,d6
        lsl.l   #2,d7
        eor.l   d6,d2
        eor.l   d7,d3

        ; Only the d2/d3 branch remains necessary.
        move.l  #$00ff00ff,d4
        move.l  d3,d7
        lsr.l   #8,d7
        eor.l   d2,d7
        and.l   d4,d7
        eor.l   d7,d2
        lsl.l   #8,d7
        eor.l   d7,d3

        ; d7 becomes plane 4; d2 becomes plane 5.
        move.l  #$55555555,d4
        move.l  d3,d7
        lsr.l   #1,d7
        eor.l   d2,d7
        and.l   d4,d7
        eor.l   d7,d2
        add.l   d7,d7
        eor.l   d3,d7

        move.l  (sp)+,d0
        move.l  d0,(a6)+
        move.l  (sp)+,d0
        move.l  d0,(a5)+
        move.l  (sp)+,d0
        move.l  d0,(a4)+
        move.l  (sp)+,d0
        move.l  d0,(a3)+

        move.l  d7,(a1)+

        cmpi.b  #6,frf_c2p_depth
        bne     .next_block
        move.l  d2,(a2)+

.next_block:
        cmpa.l  frf_c2p_rowend(pc),a0
        bne     .block

        move.l  frf_c2p_srcmod(pc),d0
        add.l   d0,a0

        move.l  frf_c2p_rowmod(pc),d0
        add.l   d0,a3
        add.l   d0,a4
        add.l   d0,a5
        add.l   d0,a6
        add.l   d0,a1

        cmpi.b  #6,frf_c2p_depth
        bne     .row_count
        add.l   d0,a2

.row_count:
        subq.w  #1,frf_c2p_rows
        bne     .row

frf_c2p56_exit:
        movem.l (sp)+,d2-d7/a2-a6
        rts

        cnop    0,4
frf_c2p_width:     ds.l    1
frf_c2p_rowend:    ds.l    1
frf_c2p_srcmod:    ds.l    1
frf_c2p_rowmod:    ds.l    1
frf_c2p_rows:      ds.w    1
frf_c2p_depth:     ds.b    1
        even
