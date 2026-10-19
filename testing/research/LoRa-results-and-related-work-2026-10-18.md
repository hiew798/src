# Preliminary Evaluation of Four LoRa Image-Transfer Modes

Prepared 18 October 2026. We conducted the hardware experiments and recorded the results below. References were checked against publisher or author/institution sources and DOI metadata.

**Same-image comparison:** we recorded five individual readings per mode using the same 2284-byte JPEG and calculated the means and sample standard deviations below. The current manuscript is [Methodology and Results](Methodology-IEEE-HD-draft.md), with IEEE and IEEE/CVF references. We retain historical unequal-payload observations separately.

## Recorded experimental results

| Mode | Method | Mean ± sample SD (s) | Timed runs | Reduction relative to mode 1 |
| --- | --- | ---: | ---: | ---: |
| 1 | Baseline stop-and-wait | 98.0 ± 8.86 | 5 | 0.0% |
| 2 | Tuned stop-and-wait | 85.8 ± 2.59 | 5 | 12.4% |
| 3 | Fixed groups with bitmap ACKs | 77.4 ± 3.65 | 5 | 21.0% |
| 4 | Adaptive policy | 65.6 ± 7.13 | 5 | 33.1% |

We started timing when the rescuer indicated image reception and stopped when it showed the photo as complete and verified. We positioned the boards side by side. Mode 4's mean was 11.8 s (15.2%) below mode 3 and 32.4 s (33.1%) below mode 1. These are descriptive differences between calculated means, not significance-test results. Mode 4 showed greater variability than mode 3, despite its lower mean.

| Mode | Recorded individual times (s) | Observed range (s) |
| --- | --- | --- |
| 1 | 94, 90, 95, 98, 113 | 90–113 |
| 2 | 85, 89, 84, 83, 88 | 83–89 |
| 3 | 75, 80, 77, 82, 73 | 73–82 |
| 4 | 58, 71, 75, 63, 61 | 58–75 |

We included every recorded reading, including 113 s in mode 1. SD uses the sample formula with denominator n − 1. We did not treat rows as paired observations across modes because pairing was not documented. We also checked all four modes over approximately 50–100 m along a clear path and observed broadly similar delivery behaviour. This distance assessment remains qualitative because per-distance timings are unavailable.

### Brick-wall follow-up — 19 October 2026

We used the same 2284-byte JPEG with the boards in adjacent rooms, approximately 1–2 m apart, with one brick wall between them. Timing used the same reception-to-complete-and-verified endpoints. We recorded four attempts per mode, and every attempt completed.

| Mode | Recorded times (s) | Mean ± sample SD (s) | Completed / attempted | Failed attempts |
| --- | --- | ---: | ---: | ---: |
| 3 — Fixed groups | 101, 116, 98, 106 | 105.25 ± 7.89 | 4 / 4 | 0 |
| 4 — Adaptive groups | 76, 101, 85, 90 | 88.00 ± 10.42 | 4 / 4 | 0 |

Mode 4's mean was 17.25 s (16.4%) lower, with greater timing variation. Both modes completed 4/4 attempts; these observations do not establish a difference in reliability or zero packet loss. The earlier side-by-side placement was not an unobstructed control at a matched distance, so differences between placements cannot be attributed solely to the wall. The standalone duplicate “76s” was confirmed to be a transcription typo and is not an additional run. The readings and correction are preserved in [Obstruction results](Obstruction-results-2026-10-19.json).

## 1. Historical observations — excluded from the current comparison

**TABLE I — EARLIER PRELIMINARY TRANSFER OBSERVATIONS**

| Mode | Transfer method | Image delivery time | Text delivery time | Measurement status |
| --- | --- | ---: | ---: | --- |
| 1 | Baseline stop-and-wait | Approximately 4 min (240 s) | Approximately 5 s | Earlier approximate observation |
| 2 | Tuned stop-and-wait | Approximately 2 min (120 s) | Approximately 5 s | Earlier approximate observation |
| 3 | Fixed groups with bitmap acknowledgements | **1 min 40 s (100 s)** | Not available | Earlier unequal-payload observation |
| 4 | Revised adaptive group selection and acknowledgement recovery | **1 min 14 s (74 s)** | Not available | Earlier unequal-payload observation |

The current source uses four chunks per fixed group. Mode 4 selects its group size using measured confirmed bytes per second and retries a missing bitmap reply before reducing the group size.

**Historical-data limitations:** Our preliminary records do not establish matching compressed sizes, repetitions, timing boundaries, firmware revisions or radio/network conditions. We therefore exclude these observations from the current four-mode comparison. The earlier mode 1 and 2 timings are rounded estimates.

### Unequal-payload pilot observations

- The two preliminary durations differ by **100 − 74 = 26 s**.
- Because the compressed images differed, neither a 26% protocol improvement nor a 35.1% goodput increase can be inferred from these timings.

Goodput means useful image bytes delivered per second, including protocol delays. It cannot be calculated here without the compressed byte count for each run. Even byte-normalised goodput would need comparable conditions and repeated trials for a fair protocol comparison.

The earlier mode 1 and mode 2 observations also lack confirmed matched payloads and conditions. Retain them as pilot timings and collect new matched measurements for the four-mode comparison.

## 2. Configuration recorded from the current source

These settings document the source configuration inspected on 18 October 2026. Per-run firmware logs are not part of the experimental record.

| Parameter | Current configuration |
| --- | --- |
| Target platform | Heltec WiFi LoRa 32 V3, ESP32-S3 with SX1262 |
| LoRa frequency | 915 MHz |
| Bandwidth | 125 kHz |
| Spreading factor | SF9 |
| Coding rate | 4/7 |
| Configured transmit power | 22 dBm |
| Preamble | 8 symbols |
| Image bytes per full chunk | 180 bytes |
| Mode 3 group size | 4 chunks |
| Mode 4 initial group size | 2 chunks |
| Mode 4 limits | At most 8 chunks, further limited by an 8 s data-airtime budget per group |
| Mode 4 trial acceptance | Measured goodput must improve by more than 5% |
| Mode 4 re-evaluation | Another size is tried after 3 measured groups at the preferred size |
| Latest adaptive source revision | `adaptive-goodput-1` |

Sources: the project's `Config.h`, `TransferConfig.h`, `TransferPolicy.h`, `LoRaMeshManager.h`, and `ADAPTIVE_UPDATE.md`. Confirm the revision from each board's startup log when collecting publishable results. The compiled adaptive maximum is a ceiling; the airtime budget can impose a smaller actual group size.

## 3. Relevant academic work

**TABLE II — RELATED WORK AND ITS RELEVANCE TO THIS IMPLEMENTATION**

| Ref. | Verified publication | Relevant method or finding | Implication for this project |
| --- | --- | --- | --- |
| [1] | Chen, Eager, and Makaroff, IEEE iThings/GreenCom/CPSCom/SmartData, 2019 | Multi-Packet LoRa batches image packets and uses bit-vector acknowledgements for selective retransmission. | Closest protocol precedent: batching and bitmap ACKs are established techniques. Compare the adaptive selection and recovery rules against a fixed-batch baseline. [Author-hosted paper](https://www.cs.usask.ca/faculty/makaroff/papers/tonghao-lorawan.pdf) |
| [2] | Torres, Nunez, and Mauricio Villanueva, IEEE INSCIT, 2024 | Multi-packet LoRa image transfer for analog meter reading reduces per-packet acknowledgement waiting. | A second precedent for faster image transfer through batching; image transmission alone is insufficient as a novelty claim. [Institutional abstract](https://cris.utec.edu.pe/es/publications/lora-multi-packet-protocol-for-image-transmission/) |
| [3] | Dede, Jalajamony, and Fernandez, IEEE CCNC, 2024 | Studies image transmission with JPEG compression and Base64 encoding/reconstruction. | Compression changes transmitted byte counts. Keep the exact compressed image constant when evaluating the transport policy. Base64 is an encoding, not a compression method. [Publisher record](https://ieeexplore.ieee.org/abstract/document/10454687) |
| [4] | Nurbay, Kasenov, Yeltay, and Zorbas, IEEE BlackSeaCom, 2024 | Cooperating nearby devices distribute transmission of a large file according to their capabilities. | Provides an alternative way to accelerate multimedia transfer, but adds cooperating nodes and is not directly comparable to a single sender–receiver link. [Institutional abstract](https://research.nu.edu.kz/en/publications/cooperative-transmission-of-large-files-over-lora-in-multimedia-l/) |
| [5] | Choi, Lee, and Lee, Symmetry, 2020 | Examines LoRa reliability using automatic repeat request (ARQ) and relay nodes, including hardware measurements. | Delivery success and recovery need evaluation alongside elapsed time. A faster successful transfer does not establish better reliability. [Institutional abstract](https://khu.elsevierpure.com/en/publications/reliability-improvement-of-lora-with-arq-and-relay-node/) |
| [6] | Calabrò, Marchetti, and Paratore, WEBIST, 2025 | Studies stop-and-wait and selective-repeat ARQ for search-and-rescue communication. | Directly relevant application context: using retransmission in search and rescue is already studied. Explain the additional adaptive mechanism and evaluate it. [Publisher paper](https://www.scitepress.org/Papers/2025/137048/137048.pdf) |

### Interpreting published performance

Chen et al. [1] report an average transmission-time reduction of 24% against stop-and-wait in their no-loss experiments. Our earlier unequal-payload pilot timings do not establish a corresponding protocol gain. Our baselines, payloads, hardware and experimental conditions also differ from that paper. The reviewed sources for [2]–[6] do not provide a matched benchmark for our implementation, so we do not assign comparable numeric performance to them.

## 4. What the contribution could be

The current implementation offers a concrete policy to investigate:

1. **Choose group size by measured useful delivery rate.** Count newly acknowledged image bytes over elapsed group time, including retries and acknowledgement waits. Keep a trial size only when it clears the improvement threshold.
2. **Recover a missing acknowledgement before shrinking the group.** Request the receiver's bitmap again after one unanswered request; repeated silence permits a smaller fallback. This avoids interpreting every missed reply as evidence that the data group is too large.
3. **Retain urgent-message priority and destination confirmation.** The source prioritizes SOS/chat traffic over images and retains persistent reception state and CRC-based image completion. Groups interrupted by urgent traffic are excluded from policy training.

These are implementation characteristics and candidate research contributions. This focused literature review does not establish that their combination is unprecedented. Demonstrating a contribution requires comparisons against relevant fixed and adaptive alternatives, repeated measurements, and an explanation of when the policy helps or hurts.

Useful follow-up experiments separate the two recent changes: original adaptive policy, acknowledgement-recovery change only, goodput-selection change only, and both changes. This would show which mechanism contributes to the observed improvement. No additional firmware changes or experiments were performed for this report.

## 5. Suggested paper wording

### Preliminary Results

We recorded five runs per mode using the same 2284-byte JPEG. Mean image-reception times were 98.0 ± 8.86 s for baseline stop-and-wait, 85.8 ± 2.59 s for tuned stop-and-wait, 77.4 ± 3.65 s for fixed grouping and 65.6 ± 7.13 s for the adaptive policy, where ± denotes sample standard deviation. We timed from the rescuer's reception indication to its complete-and-verified photo indication. The adaptive mean was 15.2% lower than fixed grouping and 33.1% lower than the baseline, but its timings varied more than those of fixed grouping. We retained every recorded reading. These are preliminary descriptive results, without a statistical-significance claim. We also observed similar delivery behaviour for all four modes over approximately 50–100 m along a clear path; this remains a qualitative observation. We excluded the earlier unequal-payload results from the current comparison.

## 6. Minimum information for the final experiment table

Use the same prepared JPEG bytes for all four modes. Record:

- Compressed byte count and file hash, to distinguish protocol gains from compression changes.
- Both boards' firmware revision, role and mode; frequency, bandwidth, spreading factor, coding rate and transmit power.
- Board separation, antenna placement, direct/relay topology, and other active traffic.
- One timing definition for every run. For radio protocol evaluation, use the image's sender `START` event through its final destination-confirmed `DELIVERED` event. Record phone compression/upload time separately if evaluating the whole user experience.
- Repeated trials per mode in a mixed order; report the count, individual results, mean/median, variability and failed/timed-out attempts.
- Retransmissions, ACK timeouts and final image CRC success. Record text latency during image transfer if claiming emergency-message responsiveness.

Do not label these observations as interference experiments without evidence of the interference conditions. Speed alone does not measure energy consumption, loss tolerance or rescue effectiveness.

## 7. IEEE-style references

[1] T. Chen, D. Eager, and D. Makaroff, “Efficient Image Transmission Using LoRa Technology In Agricultural Monitoring IoT Systems,” in *2019 International Conference on Internet of Things (iThings) and IEEE Green Computing and Communications (GreenCom) and IEEE Cyber, Physical and Social Computing (CPSCom) and IEEE Smart Data (SmartData)*, Atlanta, GA, USA, 2019, pp. 937–944, doi: [10.1109/iThings/GreenCom/CPSCom/SmartData.2019.00166](https://doi.org/10.1109/iThings/GreenCom/CPSCom/SmartData.2019.00166).

[2] J. Torres, M. Nunez, and J. M. Mauricio Villanueva, “LoRa Multi-Packet Protocol for Image Transmission,” in *2024 8th International Symposium on Instrumentation Systems, Circuits and Transducers (INSCIT)*, Joao Pessoa, Brazil, 2024, pp. 1–6, doi: [10.1109/INSCIT62583.2024.10693388](https://doi.org/10.1109/INSCIT62583.2024.10693388).

[3] O. L. Dede, H. M. Jalajamony, and R. E. Fernandez, “Image Transmission over LoRa-Based Networks: A Performance Study Using Image Compression and Reconstruction Methods,” in *2024 IEEE 21st Consumer Communications & Networking Conference (CCNC)*, Las Vegas, NV, USA, 2024, pp. 642–643, doi: [10.1109/CCNC51664.2024.10454687](https://doi.org/10.1109/CCNC51664.2024.10454687).

[4] T. Nurbay, S. Kasenov, A. Yeltay, and D. Zorbas, “Cooperative Transmission of Large Files Over LoRa in Multimedia IoT Networks,” in *2024 IEEE International Black Sea Conference on Communications and Networking (BlackSeaCom)*, Tbilisi, Georgia, 2024, pp. 153–158, doi: [10.1109/BlackSeaCom61746.2024.10646253](https://doi.org/10.1109/BlackSeaCom61746.2024.10646253).

[5] R. Choi, S. Lee, and S. Lee, “Reliability Improvement of LoRa with ARQ and Relay Node,” *Symmetry*, vol. 12, no. 4, Art. no. 552, 2020, doi: [10.3390/sym12040552](https://doi.org/10.3390/sym12040552).

[6] A. Calabrò, E. Marchetti, and M. T. Paratore, “Evaluating Use of ARQ Strategies in Communication Protocols for Search and Rescue,” in *Proceedings of the 21st International Conference on Web Information Systems and Technologies (WEBIST)*, 2025, pp. 59–70, doi: [10.5220/0013704800003985](https://doi.org/10.5220/0013704800003985).

### Verification scope

The full author-hosted paper was consulted for [1]. For [2]–[5], the technical descriptions above rely on publisher or author-institution abstracts, supported by DOI metadata. For [6], the publisher-indexed first page/abstract and citation metadata were available; the full paper was not reviewed. Bibliographic metadata for all six references was checked through Crossref. Full texts should be reviewed before a formal systematic literature review or detailed algorithm comparison.

Supporting files: `LoRa-related-work.bib` and `lora-paper-metadata-2026-10-18.json`. The metadata file also contains one additional candidate paper whose technical contents were not used here.
