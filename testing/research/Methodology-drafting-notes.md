# Notes for finalising the methodology and results

These notes are separate from the paper text.

## Current results revision — 18 October 2026

The main manuscript now includes Section IV, Results and Discussion, and Table IV. We recorded five individual timings per mode using the same 2284-byte JPEG with the boards side by side. We calculated means and sample standard deviations of 98.0 ± 8.86 s, 85.8 ± 2.59 s, 77.4 ± 3.65 s and 65.6 ± 7.13 s for modes 1–4. Timing began when the rescuer indicated reception and ended when the photo was shown as complete and verified. It excludes the preceding initial-arrival interval of approximately 3 s and does not measure the civilian's final acknowledgement time.

We preserved all 20 recorded readings in the JSON evidence file and used them for the manuscript summaries, including the 113 s mode 1 measurement. Sample SD uses n − 1 = 4 in the denominator. Mode 4 took 15.2% less time than mode 3 and 33.1% less than mode 1, based on the calculated means. Mode 4 had a higher sample SD than mode 3, so the discussion distinguishes lower average latency from more consistent timing. Acquisition order and pairing remain undocumented; no paired analysis or significance claim is made.

We also checked all four modes over approximately 50–100 m with a clear path. We describe the similar delivery behaviour as a qualitative observation. Exact per-distance values, repetition counts and failed attempts remain undocumented. The obstruction follow-up is recorded in the 19 October revision below.

The manuscript describes our completed manual comparison and the current follow-up scope. The JSON evidence record contains the recorded values and their calculated summaries, with missing measurements represented as null.

## Justification and authorship revision

The manuscript uses the research authors' voice throughout the experimental account: we conducted the hardware tests, recorded timings and calculated the summaries. The AI acknowledgement distinguishes assistance with development, writing and calculations from our experimental work.

Each numbered equation now explains its purpose and mathematical choices. The rationale covers resizing, grayscale coefficients, DCT, quantisation, chunk counting, confirmed-byte rate, group-size bounds, receiver-side timing, the mean and sample SD, and relative time reduction. Additional explanations cover estimated airtime, PSNR/SSIM, the image-size and quality limits, radio controls, persistence, timing constants, repetition counts, physical placement and obstruction comparisons. Literature supports established techniques; our heuristic constants are identified as design choices requiring sensitivity tests.

The mean and sample SD now share Eq. (9); relative reduction is Eq. (10). The existing measurements and calculated results are unchanged. The manuscript remains a detailed draft: the rationale should be retained when condensing it into the institution's ten-page template, with supporting detail moved to an appendix if that template permits one.

## Basis in the supplied HD rubric

The methodology draft addresses the HD criteria for clear processes, materials, design and testing; supporting tables; justified analysis; treatment of bias and experimental failures; and accurate referencing. It does not claim that planned tests have already been performed or that the work is guaranteed a particular grade.

The rubric also requires the supplied paper template, an overall maximum of ten pages, and acknowledgement of AI assistance. The template itself was not supplied, so final page layout and page count have not been checked. The methodology section can be condensed after the experiment scope is fixed; retain essential parameter values and measurement definitions.

## Before converting the draft into a final paper

- Record the actual JPEG identifiers, dimensions, bytes and hashes; node and antenna identities; positions; firmware revisions; and web-client workload.
- For additional obstruction trials, retain every individual timing and failed or unfinished attempt. Record wall thickness, antenna positions, a matched unobstructed placement and the same receiver-side timing endpoints. Choose a stopping rule and repetition count before testing.
- The manual comparison is the current evidence base. The source-based architecture description does not verify which firmware settings were actually flashed during those runs. No new firmware or hardware experiment is required for this writing revision.
- Earlier 100 s and 74 s results used different compressed images. They support neither a 26% protocol improvement nor a 35.1% goodput gain. The preliminary-results note has been corrected.
- Keep implementation descriptions in present tense and both completed timing experiments in past tense. Distinguish proposed follow-up procedures from completed work. The image-quality analysis remains unperformed.
- Include the required institutional AI acknowledgement. A factual starting point is: “OpenAI Codex assisted with firmware development, literature discovery and methodology drafting.” Add only review/verification statements that the author has actually completed, and follow the institution's required wording.

## Reference verification

References [1]–[13] in the revised methodology are IEEE or IEEE/CVF publications. The location revision also includes one directly relevant non-IEEE paper [14] and two W3C specifications [15], [16]. References [1]–[3], [5] and [8] are from 2024–2026. Reference [4] establishes batched LoRa image transfer with bitmap acknowledgements and documents five repetitions in its testbed study. References [6], [7] and [9] are seminal engineering sources for the DCT, JPEG and SSIM. Their inclusion follows the rubric's requirement for current and seminal literature; newer application studies do not replace the foundational explanation of a codec.

- [1]: IEEE Xplore abstract and author list, plus Crossref title, author, venue and page metadata. The page identifiers TT2.14.1–TT2.14.5 are recorded as deposited. [Publisher record](https://ieeexplore.ieee.org/abstract/document/11663995/authors).
- [2]: IEEE Xplore abstract and publication details, plus Crossref metadata. Its dual-receiver architecture is described in the abstract; no claim to have reproduced its experiments is made. [Publisher record](https://ieeexplore.ieee.org/abstract/document/11079635).
- [3]: IEEE Xplore and the authors' university record, plus Crossref metadata. [Institutional record](https://cris.utec.edu.pe/es/publications/lora-multi-packet-protocol-for-image-transmission/).
- [4]: Full author-hosted paper and Crossref metadata. [Full paper](https://www.cs.usask.ca/faculty/makaroff/papers/tonghao-lorawan.pdf).
- [5]: IEEE Xplore abstract and Crossref metadata; full text was not reviewed. Used only to establish the study's JPEG/LoRa scope. [Publisher record](https://ieeexplore.ieee.org/abstract/document/10454687).
- [6]: Original IEEE paper hosted by IIT Delhi, with DOI and bibliographic metadata checked against Crossref. [Paper](https://www.cse.iitd.ac.in/~pkalra/col783-2017/DCT-Paper.pdf).
- [7]: Original IEEE paper hosted by Stanford; the transform, quantisation and entropy-coding sections were read. Metadata checked against Crossref. [Paper](https://web.stanford.edu/class/ee398a/handouts/papers/Wallace%20-%20JPEG%20-%201992.pdf).
- [8]: The authors' [publication page](https://www.keuper-labs.org/research/2025-vision-at-night-exploring-biologically-inspired-preprocessing-for-improved-robustness-via-color-and-contrast-transf/) identifies the IEEE/CVF ICCV Workshops 2025 venue. Section 3.1 of the [version 1 author manuscript](https://arxiv.org/html/2509.24863v1) gives the same 0.299/0.587/0.114 weights. We cite only that conversion and its stated brightness rationale, not its segmentation outcomes as evidence for our radio or rescue system. Version 1 is pinned because the later arXiv version has a different title. No unverified DOI or page range was added.
- [9]: Author manuscript hosted by the authors' research laboratory; the SSIM definition was read. Published volume, issue, pages and DOI checked against Crossref. [Paper](https://www.colorado.edu/lab/live/publications/zwang_ssim_ieeeip2004.pdf).

Only the broad, verified scope of [1]–[3] and [5] is used. Their full texts were not reviewed. The draft does not claim that this literature selection proves the algorithm unprecedented. Metadata is saved in `ieee-recent-paper-metadata-2026-10-18.json`, `lora-paper-metadata-2026-10-18.json` and `ieee-compression-paper-metadata-2026-10-18.json`.

## Revision to manuscript form

The main file now contains paper prose, equations, five integrated figures and sixteen references. The separate architecture guide is supporting material and is not the manuscript. Figure 3 combines the implemented image search with a conceptual baseline JPEG encoder. Figure 4 combines the four transport paths in a single figure to limit repetition.

The conceptual JPEG stages are not claims of a custom codec. The exact browser quantisation tables, sampling factors and entropy-coding choices have not been inspected. The 15 MiB input bound controls encoded input bytes, while the 8192-byte output bound controls accepted JPEG bytes; neither establishes peak decoding memory. The 71.8 s airtime example is calculated from the current frame layout and radio parameters, not from a measured image run.

The proposed PSNR/SSIM assessment is additional research work and has not been performed. It compares the decoded JPEG with the same-size, pre-encoding grayscale canvas, so it cannot quantify the preceding resize or colour loss. No firmware, browser application or experiment tooling was modified for this manuscript revision.

The acknowledgment in the manuscript records AI assistance already provided. The author should apply the institution's required disclosure wording and verify the final submission. The institution's actual template is still unavailable; conformity with its ten-page limit has not been established.

## Phone-location revision — 19 October 2026

Section III-D describes the location scenario, literature comparison, implementation, HTTPS rationale and successful functional observation. Its contribution discussion is now Section IV-E after insertion of the obstruction results. The location comparison is Table II; the transfer comparison and side-by-side image-results table are Tables III and IV. Equations (1)–(10) and Figures 1–5 retain their previous numbering.

We confirmed that coordinates were automatically populated with mobile data disabled after continuing past the browser certificate warning. We installed no application or certificate for that step. This is recorded in `Location-observation-2026-10-19.json`; no coordinates, measurement counts, errors or unrecorded phone/browser versions were invented. No new hardware experiment was performed for this revision.

The API can accept a position cached for up to 30 seconds, and the application does not retain the provider, position timestamp or accuracy radius. The saved SOS timestamp is the report timestamp, not the position-acquisition time. The manuscript therefore avoids claiming verified GPS-chip-only operation, a fresh satellite fix, accuracy, indoor robustness or universal support for the certificate-warning workflow. Browser compatibility remains a deployment condition.

### Additional reference verification

- [10] Wireless Resilience: [IEEE publisher abstract](https://ieeexplore.ieee.org/abstract/document/10425043) and Crossref metadata reviewed. The conference year is 2023; the Xplore addition date is February 2024. Only the published architectural scope is used.
- [11] LoRaAid: [IEEE publisher abstract](https://ieeexplore.ieee.org/abstract/document/10295375) and Crossref metadata reviewed. No published accuracy value is transferred to our system.
- [12] TeamPhone: [IEEE publisher abstract](https://ieeexplore.ieee.org/abstract/document/7904658), the [author's publication page](https://z0ngqing.github.io/project/mobile/) and Crossref metadata checked.
- [13] LOCATE: [University of Bologna record](https://cris.unibo.it/handle/11585/676980), the author's publication list and Crossref metadata checked. The IEEE conference abstract supports the comparison; it does not establish details of an HTTPS implementation.
- [14] The [publisher's full paper](https://jati.apu.edu.my/index.php/JATI/article/download/170/137/300), specifically Section III on p. 57 and Section IV on p. 58, was read. The paper is not IEEE. Its printed issue is 2, even though the publisher's DOI contains `v6i1`; the bibliography preserves both correctly. The first author's name is retained as printed because surname parsing differs in the registry.
- [15], [16] The dated W3C Geolocation (2026) and Secure Contexts (2023) specifications were checked directly. They are web-platform specifications, not academic experiments. Their draft/snapshot status is stated in the references.

A 2025 IEEE disaster-device paper (DOI `10.1109/ICTMIM65579.2025.10988293`) was also found, but its primary technical text could not be verified directly from the publisher. It is not used to support a claim in this revision. The newer verified localisation comparison is LoRaAid (2024); older work is retained where it establishes the closest precedent. A publication date alone does not make a source more relevant.

No systematic search establishes that this is the first system of its kind. The claim is an implemented local HTTPS location-access path integrated with this prototype's acknowledged emergency messaging and image transfer. The quantitative image results remain unchanged. The manuscript still needs condensing and layout work for the institution's ten-page limit.

The revised paper and research files are local. No GitHub push was made for this revision.

## Brick-wall obstruction revision — 19 October 2026

We recorded four runs each for modes 3 and 4 using the same 2284-byte JPEG and the same receiver-interface timing endpoints as the side-by-side comparison. The boards were in adjacent rooms, approximately 1–2 m apart, with one brick wall between them. All four attempts in each mode completed; the failure count is zero for this obstruction session only. The earlier side-by-side and distance failure counts remain undocumented.

Mode 3 readings were 101, 116, 98 and 106 s, giving 105.25 ± 7.89 s. Mode 4 readings were 76, 101, 85 and 90 s, giving 88.00 ± 10.42 s. Sample SD uses n − 1 = 3. Mode 4's mean was lower by 17.25 s (16.4%), but its times varied more. Completion was 4/4 in both modes. This sample does not establish a difference in reliability, zero packet loss, a statistically significant advantage or universal superiority.

The extra standalone “76s” in the initial transcription was confirmed to be a typo, not a measurement. It is preserved in the raw text and correction record in `Obstruction-results-2026-10-19.json` but is not counted as a fifth run. The genuine 76 s mode-4 reading is retained. No measurements were invented or discarded as outliers.

Section III-G now describes the completed obstruction procedure; Section III-H uses n = 5 and n = 4 for the separate conditions and explains the observed completion fraction. Section IV-C and Table V contain the obstruction results. Distance observations and location discussion are Sections IV-D and IV-E. The paper does not attribute the difference between placements solely to the wall: separation and session also changed, and a matched unobstructed control was not recorded. Wall thickness, trial order, antenna orientation, packet logs and a predefined stopping rule remain undocumented.

The updated ZIP includes the obstruction data alongside the original side-by-side measurements. Changes are confined to research files and the local repository's results summary. No firmware, hardware tests or GitHub push are part of this revision.
