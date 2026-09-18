"""kaspar_arrival — Python code accompanying the arrival-process paper.

Open-source (MIT) analysis stack that ingests the four C++ tape formats
(message_tape, bbbochg_tape, packet_tape, fill_tape) produced by the
dbento_pcap_parse tools and reproduces every table and figure in the paper.

Modules:
    fill_tape  — builder for the maker-fill tape from message_tape + bbbochg_tape
    validate   — Day-1 data-integrity checks (sign-flip identity, etc.)
"""

__version__ = "0.1.0"
