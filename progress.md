# Progress

## Status
Research complete

## Tasks
- Researched native runtime/export feasibility for current-generation segmentation/video-matte PyTorch models similar to SAM3 and MatAnyone2.
- Compared TorchScript, torch.export, ONNX, ONNX Runtime CUDA, TensorRT, and LibTorch direct loading routes.
- Captured production integration guidance around external workers, memory caps, tiling/chunking, and error handling/OOM behavior.

## Files Changed
- `progress.md`: updated research status.
- `research.md`: saved evidence-backed planning notes.

## Notes
- Recommended planning direction: keep external Python worker as default for first product implementation; treat ONNX Runtime CUDA as the first native-runtime target for isolated subgraphs; TensorRT as optional optimized engine-cache path after model-specific export proof; avoid in-process LibTorch/TensorRT in render nodes unless isolated by subprocess/worker boundaries.
