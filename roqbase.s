        .text

        .align  4
roqVideo:
        .incbin "roq/commercial.roq"
roqEnd:
        .align  4

        .global _roqBase
_roqBase:
        .long   roqVideo

	.global _roqSize
_roqSize:
        .long   roqEnd - roqVideo

