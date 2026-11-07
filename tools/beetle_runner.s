# GPU measurement runner (PS-X EXE). Our own workload program, no SDK: the
# clean-room GPU measurements' runner (xg-htn.36, work/clean-room/measure/gpu),
# kept here for tools/beetle_texmeasure.py (Beetle PSX HW texture names).
#
# It waits for a request in a RAM mailbox, executes a list of operations
# (GP0 packets written by the CPU, GP0 data through DMA2, VRAM reads into RAM
# through GP0(C0h) and GPUREAD, GP1 writes), then acknowledges and waits for
# the next request. The frontend (tools/beetle_front.c through
# tools/beetle.py) writes the request through the emulator's RAM access.
#
# Mailbox at 0x80020000:
#   +0  request sequence number (the frontend bumps it after writing the list)
#   +4  acknowledged sequence number (written here when the list is done)
#   +8  'RNUR' once the runner is waiting
#   +16 the operation list:
#     0x00000000                      end
#     0x01nnnnnn w1..wn               wait idle, write n words to GP0 by CPU
#     0x02nnnnnn w1..wn               n words (n = 16k) to GP0 by DMA2 (no idle wait:
#                                     transfer data)
#     0x03000000 xy wh count dst      wait idle, GP0(C0h) xy wh, read count words to dst
#                                     (dst 0: read and drop)
#     0x04000000 w                    write w to GP1
#     0x05nnnnnn                      spin n iterations
#     0x06nnnnnn w1..wn               write n words to GP0 by CPU, no idle wait (data)
#
# Ports: psx-spx "GPU I/O Ports", "GPU Status Register" (bit 26 ready for a
# command word, bit 27 read data ready, bit 28 FIFO empty), "DMA Channels".

        .set    noat
        .text
        .globl  _start
_start:
        mfc0    $8, $12                 # interrupts off
        li      $9, 0xFFFFFFFE
        and     $8, $8, $9
        mtc0    $8, $12
        li      $29, 0x8001FF00         # stack below the mailbox
        lui     $23, 0x1F80             # I/O base
        lw      $8, 0x10F0($23)         # DPCR: enable DMA2
        ori     $8, $8, 0x0800
        sw      $8, 0x10F0($23)
        li      $22, 0x80020000         # mailbox
        li      $8, 0x52554E52
        sw      $8, 8($22)

wait:   lw      $8, 0($22)
        lw      $9, 4($22)
        beq     $8, $9, wait
        move    $21, $8                 # the request being served
        addiu   $16, $22, 16            # operation pointer

next:   lw      $8, 0($16)
        addiu   $16, $16, 4
        srl     $9, $8, 24              # operation
        li      $10, 0x00FFFFFF
        and     $10, $8, $10            # its count
        beqz    $9, done
        li      $11, 1
        beq     $9, $11, opGp0
        li      $11, 2
        beq     $9, $11, opDma
        li      $11, 3
        beq     $9, $11, opRead
        li      $11, 4
        beq     $9, $11, opGp1
        li      $11, 5
        beq     $9, $11, opSpin
        li      $11, 6
        beq     $9, $11, opData
        b       done                    # unknown: stop

done:   sw      $21, 4($22)
        b       wait

# Wait until the GPU is ready for a command word and its FIFO is empty.
waitIdle:
        li      $12, 0x14000000
1:      lw      $13, 0x1814($23)
        and     $13, $13, $12
        bne     $13, $12, 1b
        jr      $31

opGp0:  jal     waitIdle
opData:
1:      beqz    $10, next
        lw      $12, 0($16)
        addiu   $16, $16, 4
        sw      $12, 0x1810($23)
        addiu   $10, $10, -1
        b       1b

opDma:  li      $12, 0x04000002         # GP1(04h): DMA CPU to GP0
        sw      $12, 0x1814($23)
        sw      $16, 0x10A0($23)        # D2_MADR
        srl     $13, $10, 4             # blocks of 16 words
        sll     $13, $13, 16
        ori     $13, $13, 16
        sw      $13, 0x10A4($23)        # D2_BCR
        li      $12, 0x01000201         # from RAM, sync mode 1, start
        sw      $12, 0x10A8($23)        # D2_CHCR
        li      $13, 0x01000000
1:      lw      $12, 0x10A8($23)
        and     $12, $12, $13
        bnez    $12, 1b
        sll     $12, $10, 2
        addu    $16, $16, $12
        li      $12, 0x04000000         # GP1(04h): DMA off
        sw      $12, 0x1814($23)
        b       next

opRead: jal     waitIdle
        li      $12, 0xC0000000
        sw      $12, 0x1810($23)
        lw      $12, 0($16)             # xy
        sw      $12, 0x1810($23)
        lw      $12, 4($16)             # wh
        sw      $12, 0x1810($23)
        lw      $14, 8($16)             # words
        lw      $15, 12($16)            # destination
        addiu   $16, $16, 16
        li      $13, 0x08000000
1:      lw      $12, 0x1814($23)
        and     $12, $12, $13
        beqz    $12, 1b
        beqz    $15, 3f                 # destination 0: read and drop
2:      beqz    $14, next
        lw      $12, 0x1810($23)
        sw      $12, 0($15)
        addiu   $15, $15, 4
        addiu   $14, $14, -1
        b       2b
3:      beqz    $14, next
        lw      $12, 0x1810($23)
        addiu   $14, $14, -1
        b       3b

opGp1:  lw      $12, 0($16)
        addiu   $16, $16, 4
        sw      $12, 0x1814($23)
        b       next

opSpin: beqz    $10, next
        addiu   $10, $10, -1
        b       opSpin
