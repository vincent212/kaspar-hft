<!-- Generated 2026-10-02 10:43 EDT from /home/vincent/perf/mdperf/day2 -->
# Run-queue wait per thread role, us per 10 s sample (first 60s dropped)
| config | role | samples | p50 | p90 | p99 | max |
|---|---|---|---|---|---|---|
| base_A | decoder(serial idle) | 141 | 0 | 0 | 0 | 0 |
| base_A | msgbuf | 141 | 32 | 180 | 596 | 875 |
| base_A | msgproc | 243 | 0 | 0 | 0 | 0 |
| base_A | sock_reader | 159 | 4 | 3966 | 10008 | 16023 |
| base_A | tachbook | 282 | 5 | 66 | 242 | 1404 |
| mbspin_A | decoder(serial idle) | 141 | 0 | 0 | 0 | 0 |
| mbspin_A | msgbuf | 141 | 0 | 2126 | 8336 | 12669 |
| mbspin_A | msgproc | 243 | 0 | 0 | 0 | 0 |
| mbspin_A | sock_reader | 159 | 23 | 6448 | 13863 | 54067 |
| mbspin_A | tachbook | 282 | 3 | 22 | 103 | 123 |
| fastsend_mbspin_A | decoder(serial idle) | 141 | 0 | 0 | 0 | 0 |
| fastsend_mbspin_A | msgbuf | 141 | 153 | 8139 | 11438 | 30415 |
| fastsend_mbspin_A | msgproc | 243 | 0 | 0 | 0 | 0 |
| fastsend_mbspin_A | sock_reader | 159 | 250 | 6246 | 13734 | 14440 |
| fastsend_mbspin_A | tachbook | 282 | 0 | 0 | 0 | 0 |
| mbspin_tbspin_A | decoder(serial idle) | 141 | 0 | 0 | 0 | 0 |
| mbspin_tbspin_A | msgbuf | 141 | 24 | 6491 | 24245 | 28906 |
| mbspin_tbspin_A | msgproc | 243 | 0 | 0 | 0 | 0 |
| mbspin_tbspin_A | sock_reader | 159 | 7 | 2414 | 8736 | 13688 |
| mbspin_tbspin_A | tachbook | 282 | 9 | 5686 | 16422 | 55144 |
| rfs_tbspin_A | decoder(serial idle) | 141 | 0 | 0 | 0 | 0 |
| rfs_tbspin_A | msgbuf | 141 | 0 | 0 | 0 | 0 |
| rfs_tbspin_A | msgproc | 243 | 0 | 0 | 0 | 8 |
| rfs_tbspin_A | sock_reader | 159 | 8 | 2067 | 8615 | 14308 |
| rfs_tbspin_A | tachbook | 282 | 42 | 6356 | 17376 | 18728 |
| rfs_A | decoder(serial idle) | 138 | 0 | 0 | 0 | 0 |
| rfs_A | msgbuf | 138 | 0 | 0 | 0 | 0 |
| rfs_A | msgproc | 240 | 0 | 0 | 0 | 0 |
| rfs_A | sock_reader | 156 | 3 | 705 | 10468 | 17304 |
| rfs_A | tachbook | 276 | 0 | 0 | 0 | 0 |

# Worst 10 s buckets by max end-to-end book latency, with the hot thread that waited longest
| config | run | bucket max e2e us | bucket p999 us | worst thread | its runq wait us | its forced cs |
|---|---|---|---|---|---|---|
| base_A | d2_base_A | 739 | 64 | 344SketReader A | 10008 | 2135 |
| base_A | d2_base_A | 695 | 57 | 318SketReader A | 9369 | 1853 |
| base_A | d1_base_A | 203 | 193 | 310SketReader A | 3179 | 570 |
| base_A | d2_base_A | 192 | 53 | 318MsgBuf A | 596 | 78 |
| base_A | d2_base_A | 185 | 163 | TACHOB_ZNZ6 | 89 | 8 |
| base_A | d1_base_A | 156 | 149 | 344SketReader A | 1462 | 366 |
| mbspin_A | d2_mbspin_A | 823 | 39 | 344SketReader A | 29584 | 2084 |
| mbspin_A | d2_mbspin_A | 222 | 215 | 344SketReader A | 3389 | 847 |
| mbspin_A | d2_mbspin_A | 140 | 25 | 310MsgBuf A | 4543 | 1077 |
| mbspin_A | d1_mbspin_A | 139 | 128 | 318SketReader A | 5021 | 883 |
| mbspin_A | d2_mbspin_A | 127 | 23 | 344SketReader A | 2912 | 560 |
| mbspin_A | d1_mbspin_A | 126 | 115 | 318SketReader A | 5135 | 561 |
| fastsend_mbspin_A | d1_fastsend_mbspin_A | 1764 | 118 | 318SketReader A | 10649 | 858 |
| fastsend_mbspin_A | d2_fastsend_mbspin_A | 347 | 36 | 318SketReader A | 13734 | 1087 |
| fastsend_mbspin_A | d2_fastsend_mbspin_A | 270 | 24 | 318MsgBuf A | 9006 | 2467 |
| fastsend_mbspin_A | d2_fastsend_mbspin_A | 267 | 36 | 318SketReader A | 5849 | 885 |
| fastsend_mbspin_A | d2_fastsend_mbspin_A | 179 | 171 | 318MsgBuf A | 3953 | 1074 |
| fastsend_mbspin_A | d2_fastsend_mbspin_A | 114 | 105 | 318MsgBuf A | 10354 | 1943 |
| mbspin_tbspin_A | d2_mbspin_tbspin_A | 489 | 66 | 344MsgBuf A | 1493 | 355 |
| mbspin_tbspin_A | d2_mbspin_tbspin_A | 392 | 103 | 344MsgBuf A | 4799 | 1277 |
| mbspin_tbspin_A | d2_mbspin_tbspin_A | 364 | 62 | TACHOB_ZNH7 | 8355 | 2068 |
| mbspin_tbspin_A | d2_mbspin_tbspin_A | 350 | 55 | 344MsgBuf A | 11084 | 3310 |
| mbspin_tbspin_A | d2_mbspin_tbspin_A | 347 | 82 | 344SketReader A | 3898 | 517 |
| mbspin_tbspin_A | d1_mbspin_tbspin_A | 252 | 240 | TACHOB_NQH7 | 36490 | 4110 |
| rfs_tbspin_A | d1_rfs_tbspin_A | 834 | 93 | TACHOB_NQH7 | 17376 | 3138 |
| rfs_tbspin_A | d1_rfs_tbspin_A | 698 | 39 | TACHOB_NQZ6 | 7649 | 915 |
| rfs_tbspin_A | d2_rfs_tbspin_A | 386 | 33 | TACHOB_NQH7 | 17082 | 5373 |
| rfs_tbspin_A | d2_rfs_tbspin_A | 262 | 242 | TACHOB_ESH7 | 13390 | 2620 |
| rfs_tbspin_A | d2_rfs_tbspin_A | 180 | 146 | TACHOB_ZNZ6 | 6322 | 1900 |
| rfs_tbspin_A | d2_rfs_tbspin_A | 157 | 118 | TACHOB_NQZ6 | 12701 | 3353 |
| rfs_A | d2_rfs_A | 153 | 17 | 310SketReader A | 5276 | 1013 |
| rfs_A | d2_rfs_A | 148 | 30 | 318SketReader A | 252 | 11 |
| rfs_A | d1_rfs_A | 114 | 91 | 310SketReader A | 10 | 1 |
| rfs_A | d1_rfs_A | 90 | 12 | 344SketReader A | 14 | 1 |
| rfs_A | d2_rfs_A | 83 | 65 | 318SketReader A | 6 | 1 |
| rfs_A | d1_rfs_A | 82 | 70 | 310SketReader A | 705 | 27 |

# Bucket max e2e vs max hot-thread run-queue wait
| config | buckets | buckets with max e2e > 100us | of those, hot runq wait > 100us | rank corr |
|---|---|---|---|---|
| base_A | 48 | 13 | 11 | 0.09 |
| mbspin_A | 48 | 8 | 8 | 0.22 |
| fastsend_mbspin_A | 49 | 7 | 7 | 0.18 |
| mbspin_tbspin_A | 48 | 18 | 18 | -0.03 |
| rfs_tbspin_A | 49 | 11 | 11 | 0.31 |
| rfs_A | 48 | 3 | 2 | 0.34 |
