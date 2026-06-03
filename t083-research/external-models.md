# Research: T083 AI-assisted matte/mask generation and video-stable depth tools

## Summary
Best near-term Flux strategy is model-pluggable, not one-model: **SAM 2.1/Cutie for interactive video masks**, **MatAnyone/MatAnyone2 for target-assigned video alpha mattes**, and **DepthCrafter or Video Depth Anything for temporally stable depth passes**. Keep all AI inference out-of-process via Python/ONNX/Torch service writing EXR/PNG alpha/depth sequences back into Natron nodes; avoid statically linking incompatible ML code into the GPL2 C++ app.

## Ranked recommendations

1. **SAM 2.1 + Cutie first for interactive object masks** — SAM 2 is Apache-2.0, actively maintained, supports image/video promptable segmentation with `SAM2VideoPredictor`, and is the safest licensing baseline for commercial GPL2-adjacent integration as an optional external tool. Cutie is a stronger VOS tracker than older XMem/DeAOT-style propagation and includes interactive video segmentation tooling, but its exact license must be verified before bundling. Integration fit: user clicks/boxes/masks in Flux viewer, Python service returns per-frame binary/soft masks as image sequences feeding Roto/Premult/Mask branches. Sources: [SAM2 GitHub](https://github.com/facebookresearch/sam2), [SAM2 release notes](https://github.com/facebookresearch/sam2/blob/main/RELEASE_NOTES.md), [Cutie GitHub](https://github.com/hkchengrex/Cutie), [Cutie paper](https://openaccess.thecvf.com/content/CVPR2024/html/Cheng_Putting_the_Object_Back_into_Video_Object_Segmentation_CVPR_2024_paper.html).

2. **MatAnyone / MatAnyone2 for high-quality video alpha matting** — This is the strongest targeted video matting family found: MatAnyone is explicitly “stable video matting with consistent memory propagation,” and MatAnyone2 claims improved robustness/detail via a learned quality evaluator. Task fit is better than SAM for hair/fur/translucent edges because output is alpha matte rather than hard object mask. Licensing is the main blocker: GitHub reports non-standard/NOASSERTION license, so treat as research-only until LICENSE/weights terms are reviewed. Linux/Python deps are standard PyTorch/CUDA; community packaging indicates NVIDIA-only and roughly 9GB+ VRAM expectation. Sources: [MatAnyone GitHub](https://github.com/pq-yang/MatAnyone), [MatAnyone2 GitHub](https://github.com/pq-yang/MatAnyone2), [MatAnyone HF](https://huggingface.co/PeiqingYang/MatAnyone), [MatAnyone paper](https://ar5iv.labs.arxiv.org/html/2501.14677).

3. **DepthCrafter as primary video-stable depth candidate; Video Depth Anything as Apache-2.0 fallback** — DepthCrafter is purpose-built for temporally consistent long open-world video depth, without camera poses or optical flow, and appears stronger for video stability than single-frame Depth Anything-style use. However its repo shows non-standard/NOASSERTION licensing, so bundling/commercial distribution needs legal verification. Video Depth Anything is Apache-2.0 and targets consistent super-long videos, making it the safer shippable baseline even if visual quality/stability must be compared against DepthCrafter on Nick’s footage. ChronoDepth is MIT and interesting as a diffusion-prior alternative, but lower maturity/smaller adoption. Sources: [DepthCrafter GitHub](https://github.com/Tencent/DepthCrafter), [DepthCrafter paper](https://arxiv.org/html/2409.02095), [Video Depth Anything GitHub](https://github.com/DepthAnything/Video-Depth-Anything), [ChronoDepth GitHub](https://github.com/jiahao-shao1/ChronoDepth).

4. **Robust Video Matting / MODNet only as fallback/refiner, not flagship** — RVM is mature and widely used, supports PyTorch/TF/ONNX/CoreML, and outputs temporally coherent alpha for human/portrait video, but it is GPL-3.0, narrower than generic object matting, and legally awkward for a GPL2-only Natron fork if bundled. MODNet is Apache-2.0 and fast trimap-free portrait matting, useful as a low-VRAM preview/refinement option, but it is image/portrait-focused and less video-stable than newer memory-based models. Sources: [RVM GitHub](https://github.com/PeterL1n/RobustVideoMatting), [RVM inference docs](https://github.com/PeterL1n/RobustVideoMatting/blob/master/documentation/inference.md), [MODNet GitHub](https://github.com/ZHKKKe/MODNet).

5. **SAM 3 / SAM 3.1 should be watched, not adopted yet** — SAM 3 is real/current in the search results, with examples for SAM 3.1 video prediction and concept/text-prompted image/video segmentation, but its custom “SAM License” is not Apache-2.0 and likely adds commercial/usage constraints that need legal review. It may become a premium “concept segmentation” backend for text prompts, but SAM2 remains safer for an initial GPL2-compatible local integration. Sources: [SAM3 GitHub](https://github.com/facebookresearch/sam3/tree/962998a167f8146184d12c058b0f3685e844f16c), [SAM3 license](https://github.com/facebookresearch/sam3/blob/refs/heads/main/LICENSE), [SAM3.1 video example](https://github.com/facebookresearch/sam3/blob/5a3143c6/examples/sam3.1_video_predictor_example.ipynb), [SAM3 paper](https://arxiv.org/abs/2511.16719v2).

## Candidate matrix

| Candidate | Fit | License/commercial | Linux/Python deps | GPU/VRAM expectation | Inference mode | Output | Temporal stability | Flux/GPL2 integration risk |
|---|---|---|---|---|---|---|---|---|
| SAM 2.1 | Interactive image/video segmentation and mask propagation | Apache-2.0 | Python, PyTorch, CUDA; some custom CUDA paths | Good on RTX 4090; model-size dependent | Points/boxes/masks, video predictor | Masks | Good, but hard masks need alpha refinement | Low if external Python service/optional weights |
| Cutie | Robust VOS propagation/tracking after first masks | License needs verification | Python/PyTorch/CUDA | Moderate/high; benchmark locally | Semi-supervised/interactive video | Masks | Strong; designed for consistency/robustness | Medium until license/deps verified |
| XMem/XMem2 | Older robust long-term VOS / annotation helper | XMem MIT; XMem2 GPL-3.0 | Python/PyTorch, some CUDA/C++/Cython | Moderate | Masks/interactive corrections | Masks | Good legacy baseline | XMem low; XMem2 high if bundled due GPL-3.0 |
| MatAnyone/2 | Target-assigned human/object video matting | Non-standard/NOASSERTION; verify | Python/PyTorch/CUDA | Community packaging says ~9GB+ VRAM | Video with target assignment / reference guidance | Alpha matte | Very strong stated goal | High until license/weights clear; isolate out-of-process |
| RVM | Human video matting | GPL-3.0 | PyTorch/TF/ONNX | Real-time variants possible | Video RGB | Alpha matte | Good for humans | High for GPL2 bundling; use only user-installed external plugin if at all |
| MODNet | Fast portrait matte/refiner | Apache-2.0 | Python/PyTorch; ONNX ports exist | Low/moderate | Image/frame/portrait | Alpha matte | Weak unless smoothed externally | Low technically; limited task scope |
| DepthCrafter | Open-world video depth | Non-standard/NOASSERTION; verify | Python/PyTorch/diffusion stack | Likely high VRAM, slower offline | Video sequence | Depth map sequence | Strong; built for long consistent videos | Medium/high licensing + compute risk |
| Video Depth Anything | Long video depth fallback | Apache-2.0 | Python/PyTorch | Moderate/high | Video sequence | Depth map sequence | Good; explicitly video-consistent | Low/medium; quality must be compared |
| ChronoDepth | Diffusion-prior temporally consistent depth | MIT | Python/PyTorch/diffusion deps | High/slow likely | Video sequence | Depth map sequence | Promising but less mature | Medium compute/maturity risk, low license risk |
| BFRNet-like helpers | Face/background restoration prior, not primary matte/depth | Varies | Python/PyTorch | Varies | Image/video enhancement | Refined input/restored frames | Not a segmentation/depth model | Use only as optional pre-cleanup; avoid as core T083 dependency |

## Integration architecture recommendation

- Implement a **Flux AI Model Adapter** as an external process boundary: Flux sends frames/proxy frames + prompts + frame range to Python; Python writes alpha/depth EXR/PNG sequences plus JSON metadata; Flux imports them as generated footage/mask/depth layers.
- Keep model backends selectable: `sam2`, `cutie`, `matanyone`, `video-depth-anything`, `depthcrafter`, etc. Do not hardwire one model.
- For masks: store generated result as editable mask source; allow user correction at key frames and rerun propagation.
- For alpha: prefer EXR/16-bit PNG alpha output, with optional edge-refine pass.
- For depth: output normalized EXR depth + preview LUT; add deflicker/temporal smoothing and hold original model confidence/metadata where available.
- Package policy: ship Apache/MIT-compatible adapters first; for non-standard/GPL3/custom-license models, provide user-installed backend slots until license is cleared.

## Sources

- Kept: Meta SAM2 (https://github.com/facebookresearch/sam2) — safest strong promptable video segmentation baseline, Apache-2.0.
- Kept: Meta SAM3/SAM3.1 (https://github.com/facebookresearch/sam3) — current concept/video segmentation family, but license risk.
- Kept: Cutie (https://github.com/hkchengrex/Cutie) — strong recent VOS propagation candidate.
- Kept: XMem (https://github.com/hkchengrex/XMem) — MIT older VOS baseline and useful fallback.
- Kept: MatAnyone/MatAnyone2 (https://github.com/pq-yang/MatAnyone, https://github.com/pq-yang/MatAnyone2) — strongest current video matting fit, license unverified.
- Kept: DepthCrafter (https://github.com/Tencent/DepthCrafter) — strong temporally consistent video depth candidate.
- Kept: Video Depth Anything (https://github.com/DepthAnything/Video-Depth-Anything) — Apache-2.0 safer video depth backend.
- Kept: ChronoDepth (https://github.com/jiahao-shao1/ChronoDepth) — MIT alternative depth research path.
- Kept: RVM/MODNet (https://github.com/PeterL1n/RobustVideoMatting, https://github.com/ZHKKKe/MODNet) — useful fallback/refiner references.
- Dropped: random Pinokio/installer forks — useful VRAM hints only, not authoritative model sources.
- Dropped: single-frame Depth Anything-only path — Nick flagged it weak for video; use Video Depth Anything or DepthCrafter instead.

## Gaps needing verification

1. Legal review: SAM3 custom license, MatAnyone/MatAnyone2 license and model-weight terms, DepthCrafter license, Cutie license, and GPL3 contamination risk for RVM/XMem2.
2. Benchmark on Flux target hardware: RTX 4090 VRAM/time for 1080p/4K, 100/500/1000-frame clips, masks with fast motion/motion blur/hair/transparency.
3. Output quality shootout: SAM2+Cutie hard mask + alpha refinement vs MatAnyone alpha on production plates.
4. Depth shootout: DepthCrafter vs Video Depth Anything vs ChronoDepth on camera moves, cuts, low texture, VFX-friendly depth continuity.
5. Integration spike: Python service lifecycle, cancellation, progress reporting, cache invalidation, EXR sequence import, and user correction/re-propagation UX.
