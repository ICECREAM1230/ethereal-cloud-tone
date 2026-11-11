# PCB review checklist

Read only when reviewing actual DRC/ERC output or board constraints. The skill entrypoint and shared execution contract govern scope; examples are conditional on the current request.

## DRC/ERC Interpretation

### Common DRC Errors
| Error | Meaning | Fix |
|---|---|---|
| clearance_violation | Items too close | Increase spacing |
| shorting_items | Different nets touching | Reroute traces |
| courtyards_overlap | Components too close | Move apart |
| track_width | Outside allowed range | Adjust width |
| annular_width | Via ring too small | Increase via size |
| copper_sliver | Thin copper (mfg risk) | Adjust pour |
| hole_near_hole | Drilled holes too close | Increase spacing |

### Common ERC Errors
| Error | Fix |
|---|---|
| Input power pin not driven | Add PWR_FLAG |
| Pin not connected | Connect or add no-connect X |
| Conflicting pin types | Check symbol pin types |
| Duplicate reference | Re-annotate schematic |

## AI Review Checklist

### Decoupling
- [ ] Every IC: 100nF cap within 5mm of power pins
- [ ] Bulk caps near power entry
- [ ] Current path: Supply -> Cap -> IC pin

### Power
- [ ] Continuous ground plane
- [ ] No signal traces cutting ground under high-speed signals
- [ ] Adequate copper weight for current

### High-Speed
- [ ] Controlled impedance for >50MHz signals
- [ ] Length matching for differential pairs
- [ ] Via stitching along high-speed paths

### Thermal
- [ ] Thermal vias under exposed pads (min 4-9)
- [ ] Heat sources away from temp-sensitive components

### Manufacturing
- [ ] Board outline closed on Edge.Cuts
- [ ] Min trace >= 0.1mm, min drill >= 0.2mm
- [ ] Silkscreen not over pads
- [ ] Fiducials present (min 3)

## Confidence Scoring

| Level | Criteria | DRC Expectation |
|---|---|---|
| HIGH | <50 components, 2-layer, <10MHz | Zero violations |
| MEDIUM | 50-200 components, 2-4 layer, <100MHz | May need adjustments |
| LOW | >200 components, 4+ layers, high-speed/RF/power | Expert review required |

Override to LOW if: RF/antenna, DDR/PCIe/USB3+, SMPS >5W, BGA >100 pins.
