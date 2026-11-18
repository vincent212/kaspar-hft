# Technical reports

| Paper | PDF | Source |
|---|---|---|
| Adapting the actor model of concurrency for high-frequency trading ([arXiv:2609.21173](https://arxiv.org/abs/2609.21173)) | [fast_send.pdf](fast_send.pdf) | [tex/fast_send/](tex/fast_send/) |
| Model-free passive execution via order-level shadowing ([arXiv:2609.18019](https://arxiv.org/abs/2609.18019)) | [shadow_pov.pdf](shadow_pov.pdf) | [tex/shadow_pov/](tex/shadow_pov/) |
| Adapting the actor model of concurrency to hybrid CPU/FPGA architectures | [fpga_actors.pdf](fpga_actors.pdf) | [tex/fpga_actors/](tex/fpga_actors/) |
| Extending the actor model with synchronous delivery | [extended_actor_model.pdf](extended_actor_model/extended_actor_model.pdf) | [tex/extended_actor_model/](tex/extended_actor_model/) |
| kaspar-hft one-pager | [kaspar_onepager.pdf](kaspar_onepager.pdf) | [tex/kaspar_onepager/](tex/kaspar_onepager/) |

`make` builds every paper in its source folder and copies the PDF to the path in
the table; `make <paper>` builds one.

Articles and measurement write-ups are in [articles/](articles/):

| Article | |
|---|---|
| [queue_types/](articles/queue_types/) | mailbox queue types: design, benchmarks on Apple M3 and AMD EPYC, runbook |
| [md_latency_article.md](articles/md_latency_article.md) | market-data handler latency |
| [serial_vs_parallel_decode.md](articles/serial_vs_parallel_decode.md) | serial versus parallel MDP3 decode, measured |
