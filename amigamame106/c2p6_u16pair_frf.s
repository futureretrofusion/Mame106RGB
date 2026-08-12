;
; FRF_NATIVE_EHB_FUSED_PAIR_V6
;
; EHB-only fused indexed remap + six-plane C2P.
;
; Input pixels are MAME indexed UWORD values in the range 0..255.
; A 65536-entry UWORD pair table maps two source indices directly to two
; EHB pen bytes. This removes the full-frame UWORD -> UBYTE intermediate
; pass for unscaled, unrotated indexed games.
;
; Register ABI:
; a0 source UWORD pixels
; a1 destination BitMap
; a2 UWORD pair lookup table
; d0.w width in pixels, multiple of 32
; d1.w height
; d2.w destination X, multiple of 8
; d3.w destination Y
; d4.l source bytes per row
;
; Non-reentrant, matching the existing native C2P routines.
;

        XDEF    _frf_c2p6_u16pair

        incdir  include:
        include graphics/gfx.i

        section code,code

_frf_c2p6_u16pair:
        movem.l d2-d7/a2-a6,-(sp)

        cmpi.b  #6,bm_Depth(a1)
        bne     frf_fused_exit
        tst.w   d1
        beq     frf_fused_exit
        tst.w   d0
        beq     frf_fused_exit

        move.w  d0,d5
        andi.w  #$001f,d5
        bne     frf_fused_exit

        move.w  d2,d5
        andi.w  #$0007,d5
        bne     frf_fused_exit

        move.l  a2,frf_fused_pairlut
        move.w  d1,frf_fused_rows

        moveq   #0,d5
        move.w  d0,d5
        move.l  d5,frf_fused_width

        add.l   d5,d5
        move.l  d4,d6
        sub.l   d5,d6
        move.l  d6,frf_fused_srcmod

        moveq   #0,d6
        move.w  bm_BytesPerRow(a1),d6

        moveq   #0,d5
        move.w  d0,d5
        lsr.l   #3,d5

        move.l  d6,d7
        sub.l   d5,d7
        move.l  d7,frf_fused_rowmod

        moveq   #0,d7
        move.w  d3,d7
        mulu.w  d6,d7

        moveq   #0,d5
        move.w  d2,d5
        lsr.l   #3,d5
        add.l   d5,d7

        movem.l bm_Planes(a1),a2-a6

        add.l   d7,a2
        add.l   d7,a3
        add.l   d7,a4
        add.l   d7,a5
        add.l   d7,a6

        move.l  bm_Planes+20(a1),d5
        add.l   d7,d5
        move.l  d5,frf_fused_plane5

        move.l  frf_fused_pairlut(pc),a1

frf_fused_row:
        move.l  a0,d0
        move.l  frf_fused_width(pc),d5
        add.l   d5,d5
        add.l   d5,d0
        move.l  d0,frf_fused_rowend

frf_fused_block:
        move.l  (a0)+,d0
        move.l  d0,d1
        lsr.l   #8,d1
        or.w    d0,d1
        moveq   #0,d2
        move.w  (a1,d1.l*2),d2
        swap    d2
        move.l  (a0)+,d0
        move.l  d0,d1
        lsr.l   #8,d1
        or.w    d0,d1
        move.w  (a1,d1.l*2),d2
        move.l  d2,-(sp)
        move.l  (a0)+,d0
        move.l  d0,d1
        lsr.l   #8,d1
        or.w    d0,d1
        moveq   #0,d2
        move.w  (a1,d1.l*2),d2
        swap    d2
        move.l  (a0)+,d0
        move.l  d0,d1
        lsr.l   #8,d1
        or.w    d0,d1
        move.w  (a1,d1.l*2),d2
        move.l  d2,-(sp)
        move.l  (a0)+,d0
        move.l  d0,d1
        lsr.l   #8,d1
        or.w    d0,d1
        moveq   #0,d2
        move.w  (a1,d1.l*2),d2
        swap    d2
        move.l  (a0)+,d0
        move.l  d0,d1
        lsr.l   #8,d1
        or.w    d0,d1
        move.w  (a1,d1.l*2),d2
        move.l  d2,-(sp)
        move.l  (a0)+,d0
        move.l  d0,d1
        lsr.l   #8,d1
        or.w    d0,d1
        moveq   #0,d2
        move.w  (a1,d1.l*2),d2
        swap    d2
        move.l  (a0)+,d0
        move.l  d0,d1
        lsr.l   #8,d1
        or.w    d0,d1
        move.w  (a1,d1.l*2),d2
        move.l  d2,-(sp)
        move.l  (a0)+,d0
        move.l  d0,d1
        lsr.l   #8,d1
        or.w    d0,d1
        moveq   #0,d2
        move.w  (a1,d1.l*2),d2
        swap    d2
        move.l  (a0)+,d0
        move.l  d0,d1
        lsr.l   #8,d1
        or.w    d0,d1
        move.w  (a1,d1.l*2),d2
        move.l  d2,-(sp)
        move.l  (a0)+,d0
        move.l  d0,d1
        lsr.l   #8,d1
        or.w    d0,d1
        moveq   #0,d2
        move.w  (a1,d1.l*2),d2
        swap    d2
        move.l  (a0)+,d0
        move.l  d0,d1
        lsr.l   #8,d1
        or.w    d0,d1
        move.w  (a1,d1.l*2),d2
        move.l  d2,-(sp)
        move.l  (a0)+,d0
        move.l  d0,d1
        lsr.l   #8,d1
        or.w    d0,d1
        moveq   #0,d2
        move.w  (a1,d1.l*2),d2
        swap    d2
        move.l  (a0)+,d0
        move.l  d0,d1
        lsr.l   #8,d1
        or.w    d0,d1
        move.w  (a1,d1.l*2),d2
        move.l  d2,-(sp)
        move.l  (a0)+,d0
        move.l  d0,d1
        lsr.l   #8,d1
        or.w    d0,d1
        moveq   #0,d2
        move.w  (a1,d1.l*2),d2
        swap    d2
        move.l  (a0)+,d0
        move.l  d0,d1
        lsr.l   #8,d1
        or.w    d0,d1
        move.w  (a1,d1.l*2),d2
        move.l  d2,-(sp)
        move.l  a0,frf_fused_srcnext

        lea     32(sp),a0
        move.l  -(a0),d0
        move.l  -(a0),d2
        move.l  -(a0),d1
        move.l  -(a0),d3

        move.l  #$0f0f0f0f,d6
        and.l   d6,d0
        and.l   d6,d1
        and.l   d6,d2
        and.l   d6,d3
        lsl.l   #4,d0
        lsl.l   #4,d1
        or.l    d2,d0
        or.l    d3,d1

        move.l  -(a0),d2
        move.l  -(a0),d6
        move.l  -(a0),d3
        move.l  -(a0),d7

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


        lea     48(sp),a0
        move.l  -(a0),d0
        move.l  -(a0),d2
        move.l  -(a0),d1
        move.l  -(a0),d3

        move.l  #$f0f0f0f0,d6
        and.l   d6,d0
        and.l   d6,d1
        and.l   d6,d2
        and.l   d6,d3
        lsr.l   #4,d2
        lsr.l   #4,d3
        or.l    d2,d0
        or.l    d3,d1

        move.l  -(a0),d2
        move.l  -(a0),d6
        move.l  -(a0),d3
        move.l  -(a0),d7

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
        move.l  d0,(a5)+
        move.l  (sp)+,d0
        move.l  d0,(a4)+
        move.l  (sp)+,d0
        move.l  d0,(a3)+
        move.l  (sp)+,d0
        move.l  d0,(a2)+

        move.l  d7,(a6)+

        lea     32(sp),sp

        move.l  frf_fused_plane5(pc),a0
        move.l  d2,(a0)+
        move.l  a0,frf_fused_plane5

        move.l  frf_fused_srcnext(pc),a0
        cmpa.l  frf_fused_rowend(pc),a0
        bne     frf_fused_block

        move.l  frf_fused_srcmod(pc),d0
        add.l   d0,a0

        move.l  frf_fused_rowmod(pc),d0
        add.l   d0,a2
        add.l   d0,a3
        add.l   d0,a4
        add.l   d0,a5
        add.l   d0,a6

        move.l  frf_fused_plane5(pc),d5
        add.l   d0,d5
        move.l  d5,frf_fused_plane5

        subq.w  #1,frf_fused_rows
        bne     frf_fused_row

frf_fused_exit:
        movem.l (sp)+,d2-d7/a2-a6
        rts

        cnop    0,4
frf_fused_pairlut: ds.l    1
frf_fused_plane5: ds.l    1
frf_fused_srcnext:ds.l    1
frf_fused_width:  ds.l    1
frf_fused_rowend: ds.l    1
frf_fused_srcmod: ds.l    1
frf_fused_rowmod: ds.l    1
frf_fused_rows:   ds.w    1
        even
