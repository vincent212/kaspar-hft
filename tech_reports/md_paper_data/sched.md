<!-- Generated 2026-10-01 10:55 EDT from /home/vincent/perf/mdperf/paper -->
# Run-queue wait per thread role, us per 10 s sample (first 120s dropped)
| config | role | samples | p50 | p90 | p99 | max |
|---|---|---|---|---|---|---|
| base | decoder(serial idle) | 249 | 0 | 0 | 0 | 0 |
| base | msgbuf | 249 | 59 | 297 | 1160 | 10194 |
| base | msgproc | 426 | 0 | 0 | 0 | 5 |
| base | sock_reader | 534 | 8 | 2516 | 7661 | 14718 |
| base | tachbook | 498 | 7 | 85 | 272 | 844 |
| fastsend | decoder(serial idle) | 252 | 0 | 0 | 0 | 0 |
| fastsend | msgbuf | 252 | 36 | 237 | 1108 | 2731 |
| fastsend | msgproc | 429 | 0 | 0 | 0 | 19 |
| fastsend | sock_reader | 546 | 12 | 1991 | 11206 | 14841 |
| fastsend | tachbook | 504 | 0 | 0 | 0 | 0 |
| mbspin | decoder(serial idle) | 246 | 0 | 0 | 0 | 0 |
| mbspin | msgbuf | 246 | 0 | 2471 | 12854 | 15539 |
| mbspin | msgproc | 423 | 0 | 0 | 0 | 34 |
| mbspin | sock_reader | 528 | 33 | 3458 | 11244 | 19199 |
| mbspin | tachbook | 492 | 8 | 79 | 234 | 1351 |
| fastsend_mbspin | decoder(serial idle) | 144 | 0 | 0 | 0 | 0 |
| fastsend_mbspin | msgbuf | 144 | 66 | 5662 | 12727 | 15604 |
| fastsend_mbspin | msgproc | 246 | 0 | 0 | 0 | 8 |
| fastsend_mbspin | sock_reader | 306 | 10 | 2790 | 12418 | 17053 |
| fastsend_mbspin | tachbook | 288 | 0 | 0 | 0 | 0 |
| p4s | msgbuf | 198 | 139 | 1082 | 4611 | 12672 |
| p4s | msgproc | 339 | 37 | 348 | 1362 | 4830 |
| p4s | par_handler | 198 | 146 | 2427 | 13027 | 24019 |
| p4s | par_worker | 792 | 4 | 3405 | 15600 | 55910 |
| p4s | sock_reader | 414 | 67 | 2526 | 9880 | 17087 |
| p4s | tachbook | 396 | 14 | 148 | 2273 | 7077 |
| base_A | decoder(serial idle) | 108 | 0 | 0 | 0 | 0 |
| base_A | msgbuf | 108 | 58 | 331 | 674 | 998 |
| base_A | msgproc | 183 | 0 | 0 | 0 | 0 |
| base_A | sock_reader | 117 | 25 | 4304 | 6988 | 10858 |
| base_A | tachbook | 216 | 8 | 105 | 275 | 1862 |
| fastsend_A | decoder(serial idle) | 105 | 0 | 0 | 0 | 0 |
| fastsend_A | msgbuf | 105 | 13 | 82 | 229 | 1021 |
| fastsend_A | msgproc | 180 | 0 | 0 | 0 | 0 |
| fastsend_A | sock_reader | 114 | 4 | 3430 | 13933 | 20122 |
| fastsend_A | tachbook | 210 | 0 | 0 | 0 | 0 |
| fastsend_mbspin_A | decoder(serial idle) | 105 | 0 | 0 | 0 | 0 |
| fastsend_mbspin_A | msgbuf | 105 | 0 | 2967 | 5543 | 10881 |
| fastsend_mbspin_A | msgproc | 180 | 0 | 0 | 0 | 14 |
| fastsend_mbspin_A | sock_reader | 114 | 22 | 2801 | 6168 | 12001 |
| fastsend_mbspin_A | tachbook | 210 | 0 | 0 | 0 | 0 |
| p4s_A | msgbuf | 108 | 178 | 830 | 1302 | 3240 |
| p4s_A | msgproc | 183 | 51 | 372 | 918 | 1082 |
| p4s_A | par_handler | 108 | 0 | 3 | 12 | 5172 |
| p4s_A | par_worker | 432 | 0 | 1852 | 4581 | 16301 |
| p4s_A | sock_reader | 117 | 1334 | 4324 | 9257 | 15271 |
| p4s_A | tachbook | 216 | 17 | 127 | 314 | 364 |

# Worst 10 s buckets by max end-to-end book latency, with the hot thread that waited longest
| config | run | bucket max e2e us | bucket p999 us | worst thread | its runq wait us | its forced cs |
|---|---|---|---|---|---|---|
| base | p2_base | 10087 | 2893 | 318MsgBuf A | 10194 | 5 |
| base | p1_base | 2730 | 993 | TACHOB_ZNZ6 | 537 | 5 |
| base | p1_base | 2502 | 981 | 344SketReader A | 5075 | 974 |
| base | p1_base | 1875 | 38 | 318SketReader A | 3979 | 660 |
| base | p1_base | 1164 | 861 | 318SketReader A | 1876 | 318 |
| base | p1_base | 946 | 30 | 344SketReader B | 5618 | 900 |
| fastsend | p1_fastsend | 3087 | 2791 | 318SketReader A | 3976 | 595 |
| fastsend | p1_fastsend | 1946 | 866 | 344SketReader A | 1936 | 352 |
| fastsend | p1_fastsend | 1925 | 1025 | 310SketReader B | 1260 | 225 |
| fastsend | p1_fastsend | 1631 | 1000 | 318MsgBuf A | 42 | 2 |
| fastsend | p1_fastsend | 1353 | 824 | 310SketReader B | 406 | 0 |
| fastsend | p1_fastsend | 1268 | 35 | 344SketReader A | 4652 | 843 |
| mbspin | p1_mbspin | 2788 | 90 | 344SketReader A | 9739 | 1614 |
| mbspin | p1_mbspin | 1987 | 118 | 318SketReader B | 11244 | 2633 |
| mbspin | p2_mbspin | 1207 | 48 | 310MsgBuf A | 2232 | 583 |
| mbspin | p1_mbspin | 656 | 44 | 310SketReader B | 1165 | 298 |
| mbspin | p2_mbspin | 396 | 59 | 318SketReader A | 11068 | 1661 |
| mbspin | p2_mbspin | 378 | 31 | 344MsgBuf A | 15413 | 2213 |
| fastsend_mbspin | p1_fastsend_mbspin | 3440 | 379 | 344MsgBuf A | 6496 | 1623 |
| fastsend_mbspin | p1_fastsend_mbspin | 1755 | 329 | 318MsgBuf A | 11337 | 1979 |
| fastsend_mbspin | p1_fastsend_mbspin | 1727 | 146 | 318SketReader A | 17053 | 1959 |
| fastsend_mbspin | p1_fastsend_mbspin | 574 | 140 | 318SketReader A | 2897 | 383 |
| fastsend_mbspin | p1_fastsend_mbspin | 383 | 42 | 318SketReader A | 7306 | 1375 |
| fastsend_mbspin | p1_fastsend_mbspin | 376 | 17 | 318SketReader B | 2180 | 168 |
| p4s | p2_p4s | 13053 | 9984 | DataActor_344_0 | 6181 | 817 |
| p4s | p2_p4s | 10736 | 9637 | DataActor_344_0 | 55910 | 3289 |
| p4s | p1_p4s | 7190 | 3869 | 344MsgBuf A | 12672 | 21 |
| p4s | p1_p4s | 2832 | 1183 | 318SketReader A | 4668 | 703 |
| p4s | p1_p4s | 1341 | 82 | 318SketReader A | 8778 | 1766 |
| p4s | p1_p4s | 1333 | 198 | HandIfActor_310 | 10680 | 1715 |
| base_A | p2_base_A | 1651 | 1006 | 344SketReader A | 3314 | 250 |
| base_A | p2_base_A | 930 | 280 | 344SketReader A | 5629 | 1587 |
| base_A | p2_base_A | 667 | 73 | 310SketReader A | 1094 | 243 |
| base_A | p2_base_A | 616 | 26 | 344SketReader A | 4381 | 1241 |
| base_A | p2_base_A | 440 | 92 | 318MsgBuf A | 674 | 26 |
| base_A | p2_base_A | 362 | 20 | 344SketReader A | 10858 | 933 |
| fastsend_A | p2_fastsend_A | 389 | 59 | 344SketReader A | 1270 | 170 |
| fastsend_A | p2_fastsend_A | 381 | 232 | 310MsgBuf A | 93 | 8 |
| fastsend_A | p2_fastsend_A | 380 | 20 | 344SketReader A | 4980 | 986 |
| fastsend_A | p2_fastsend_A | 374 | 38 | 318MsgBuf A | 22 | 2 |
| fastsend_A | p2_fastsend_A | 333 | 19 | 318MsgBuf A | 25 | 1 |
| fastsend_A | p2_fastsend_A | 299 | 40 | 318MsgBuf A | 327 | 1 |
| fastsend_mbspin_A | p2_fastsend_mbspin_A | 394 | 18 | 318SketReader A | 46 | 10 |
| fastsend_mbspin_A | p2_fastsend_mbspin_A | 394 | 12 | 318SketReader A | 3012 | 640 |
| fastsend_mbspin_A | p2_fastsend_mbspin_A | 394 | 36 | 318SketReader A | 3460 | 758 |
| fastsend_mbspin_A | p2_fastsend_mbspin_A | 382 | 38 | 318SketReader A | 4015 | 650 |
| fastsend_mbspin_A | p2_fastsend_mbspin_A | 368 | 17 | 344MsgBuf A | 5543 | 1337 |
| fastsend_mbspin_A | p2_fastsend_mbspin_A | 364 | 87 | 344SketReader A | 6483 | 1826 |
| p4s_A | p2_p4s_A | 2615 | 195 | DataActor_318_1 | 2703 | 719 |
| p4s_A | p2_p4s_A | 648 | 48 | 310SketReader A | 1378 | 349 |
| p4s_A | p2_p4s_A | 515 | 58 | 310SketReader A | 2136 | 519 |
| p4s_A | p2_p4s_A | 503 | 49 | 344SketReader A | 2579 | 659 |
| p4s_A | p2_p4s_A | 441 | 37 | 310SketReader A | 15271 | 2178 |
| p4s_A | p2_p4s_A | 430 | 48 | 318SketReader A | 2585 | 442 |

# Bucket max e2e vs max hot-thread run-queue wait
| config | buckets | buckets with max e2e > 100us | of those, hot runq wait > 100us | rank corr |
|---|---|---|---|---|
| base | 85 | 24 | 24 | 0.42 |
| fastsend | 84 | 28 | 24 | -0.12 |
| mbspin | 83 | 28 | 28 | 0.16 |
| fastsend_mbspin | 49 | 16 | 16 | 0.26 |
| p4s | 67 | 44 | 44 | 0.31 |
| base_A | 36 | 15 | 14 | 0.14 |
| fastsend_A | 36 | 9 | 5 | 0.15 |
| fastsend_mbspin_A | 36 | 15 | 11 | 0.43 |
| p4s_A | 36 | 26 | 26 | 0.03 |
