        .text

        .align  4

        .global _roqBase
_roqBase:
        .incbin "mansion.roq"
_roqBase_end = .

        .align  4

	.global _roqSize
_roqSize:
        .long _roqBase_end-_roqBase
