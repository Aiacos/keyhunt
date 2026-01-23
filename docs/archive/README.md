# Archived Development Plans

This directory contains historical development plans and design documents from keyhunt development.

These documents have historical value and show the evolution of keyhunt's features, but many have been superseded by actual implementations. For current documentation, see the [wiki](../wiki/README.md).

## Archived Documents

| Document | Date | Status | Description |
|----------|------|--------|-------------|
| [Bottleneck Analysis](2026-01-21-bottleneck-analysis.md) | 2026-01-21 | Completed | Performance bottleneck analysis |
| [Hybrid Performance Design](2026-01-21-hybrid-performance-optimization-design.md) | 2026-01-21 | Completed | GPU hybrid mode design |
| [Wizard Design](2026-01-21-wizard-design.md) | 2026-01-21 | Completed | Interactive wizard architecture |
| [Wizard Implementation](2026-01-21-wizard-implementation.md) | 2026-01-21 | Completed | Wizard implementation details |
| [Distributed Protocol](2026-01-22-distributed-protocol-enhancements.md) | 2026-01-22 | Completed | TCP+JSON protocol design |
| [Hybrid Performance Remaining](2026-01-22-hybrid-performance-remaining.md) | 2026-01-22 | Partial | Outstanding optimization tasks |
| [PrivateKeys.pw Integration](2026-01-22-privatekeys-pw-implementation-plan.md) | 2026-01-22 | Completed | Community progress integration |
| [PrivateKeys.pw Design](2026-01-22-privatekeys-pw-integration-design.md) | 2026-01-22 | Completed | Community integration design |
| [Source Reorganization](2026-01-22-src-reorganization.md) | 2026-01-22 | Completed | Modular src/ structure |
| [Usability Improvements](2026-01-22-usability-improvements.md) | 2026-01-22 | Completed | Help, output, progress, benchmark |

## Implementation Status

Most features described in these plans have been implemented:

- **Interactive Wizard**: Implemented in `wizard/` directory
- **Distributed Mode**: Implemented in `distributed/` directory
- **GPU Hybrid Mode**: Implemented in `gpu/` directory
- **Parameter Validation**: Implemented in `parameter_validator.c/h`
- **Modular Components**: Implemented in `src/` directory

## Note

These documents may contain outdated information, superseded decisions, or abandoned approaches. Always refer to the [current documentation](../wiki/README.md) and source code for accurate information.
