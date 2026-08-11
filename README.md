# Coexistence of 802.11bf and 802.11ax

An ns-3 extension for **Integrated Sensing and Communication (ISAC)** and **flexible Trigger-Based Wi-Fi Sensing** in sub-7 GHz IEEE 802.11bf networks.

This repository builds on the open-source baseline implementation of [Lumbantobing et al.](https://github.com/idolumbantobing/IEEE_802.11bf) (ns-3 3.40), which introduced the first MAC-layer 802.11bf Trigger-Based (TB) sensing procedure for ns-3, covering the Polling, NDPA Sounding, and Reporting phases with a single Sensing Measurement Exchange (SME) per Sensing Availability Window (SAW).

This project extends that baseline to close several gaps left unaddressed by prior open-source work: ISAC, multiple SMEs per SAW, the SR2SI TF Sounding phase, and configurable sensing phase sequencing, broadening the range of scenarios that can be studied through simulation.

## Contributions

- **ISAC enablement** - Integrates data and sensing communication within the same IEEE 802.11bf network, so application traffic and sensing activity coexist and share the medium instead of being modeled as separate networks.
- **Multiple SMEs per SAW** - Reinterprets the sensing window as a true recurring SAW capable of completing several Sensing Measurement Exchanges, instead of one fixed-duration exchange per window.
- **SR2SI TF Sounding phase** - Implements the Trigger Frame Sounding phase (Sensing Responder → Sensing Initiator variant), enabling AP-side CSI collection, which the baseline did not support.
- **Dynamic sensing phase sequence** - Replaces the baseline's static phase order with a configurable, user-defined sequence (`SensingPhaseSequence`), enabling STA-side, AP-side, or combined CSI collection strategies within a single SAW.

> For the full protocol background, implementation details, and evaluation results behind these contributions, see the accompanying paper (citation below).

## Background: IEEE 802.11bf Trigger-Based Sensing

A brief primer to orient the parameters used throughout this repo:

- **Sensing session** - the overall context (capabilities, bandwidth, SAW duration/interval) agreed between AP and STAs. Session setup/negotiation is **not implemented** here; parameters are fixed at simulation start via command-line arguments instead (see [Sensing session setup](#sensing-session-setup) under Limitations).
- **SAW (Sensing Availability Window)** - a periodically recurring window, with a fixed `SensingDurationMs` and recurrence `SensingIntervalMs`, during which the channel is reserved exclusively for sensing and any ongoing data transfer is suspended.
- **SME (Sensing Measurement Exchange)** - one or more SMEs occur within a SAW. Each SME is a sequence of sensing phases:
  - **Polling** - AP confirms STA availability (CTS-to-self response).
  - **NDPA Sounding** - AP sends an NDPA + NDP; STAs extract CSI (STA-side collection).
  - **TF Sounding (SR2SI)** - AP sends a Sensing Trigger; STAs respond with an NDP that the AP uses to extract CSI (AP-side collection).
  - **Reporting** - STAs report CSI results back to the AP. In this implementation, Reporting follows automatically and unconditionally after any NDPA Sounding phase, so it is not itself a configurable token in the sequence.

### Sensing Phase Sequence

The `sensingPhaseSequence` parameter is a comma-separated, ordered list of phases executed within a single SAW, configurable directly from the command line (e.g. `"POLLING,NDPA_SOUNDING,TF_SOUNDING"`). It follows the standard's structural constraints for TB SMEs where a sequence **cannot** consist solely of:

- A Reporting phase alone.
- An NDPA Sounding phase alone.
- An SR2SR TF Sounding phase alone in which the sensing responder is a receiver (not implemented - this repo only supports SR2SI).
- Only a Polling phase followed by a Reporting phase.

Within those constraints, valid sequences include, for example:

| Sequence | Enables |
|---|---|
| `POLLING,NDPA_SOUNDING,TF_SOUNDING` | Both AP-side and STA-side CSI collection (Reporting follows the NDPA phase automatically) |
| `POLLING,TF_SOUNDING` | AP-side CSI collection only, at the highest SME sampling rate |
| `POLLING,TF_SOUNDING,TF_SOUNDING` | Repeated AP-side sounding within the same SAW |
| `POLLING,NDPA_SOUNDING` | STA-side CSI collection only (the baseline's original, fixed sequence) |

Accepted tokens are `POLLING`, `NDPA_SOUNDING`, and `TF_SOUNDING`; phases may repeat.

## Getting Started

## Running Simulations

The example scenario lives at [`examples/wireless/wifi-bf-network.cc`](examples/wireless/wifi-bf-network.cc). It sets up BSSs with a configurable AP/STA topology, drives both sensing (802.11bf) and data (802.11ax) traffic, and reports throughput and sensing KPIs.

### Single run example

```bash
./ns3 run "wifi-bf-network \
    --nBss=1 \
    --nBfBss=1 \
    --nStations=2 \
    --simulationTime=10 \
    --sensingDurationMs=51.2 \
    --sensingIntervalMs=307.2 \
    --sensingPhaseSequence=POLLING,NDPA_SOUNDING,TF_SOUNDING \
    --enableCsvOutput=true --csvFilename=results.csv"
```

### Key configurable parameters

| Category | Parameters |
|---|---|
| Topology | `nBss`, `nBfBss`, `nAxBss`, `nStations`, `nStations_net2`, `scenario` (`CIRCULAR_PLACEMENT`, `PREDEFINED_LOCATIONS`, `INDOOR_OFFICE_ENVIRONMENT`, `RANDOM_PLACEMENT`, `INDOOR_OFFICE_3GPP`, `VOLUMETRIC_VIDEO_STREAMING`), `radius`, `residentialDensity` |
| Sensing (802.11bf) | `enableWiFiSensing`, `sensingDurationMs`, `sensingIntervalMs`, `sensingIntervalType`, `sensingPhaseSequence`, `soundingtype` |
| Channel sounding | `enableMuMIMO`, `maxNumDlMuMimoSta`, `channelSoundingInterval`, `ngSu`, `ngMu`, `codebookSizeSu`, `codebookSizeMu`, `nc`, `csMode` |
| PHY / channel | `frequency`, `channelWidth`, `mcs`, `enableFading` (Nakagami small-scale fading) |
| Data traffic | `udp`, `downlink`, `payloadSize`, `trafficType`, `enableFrameAggregation` |
| Simulation control | `simulationTime`, `seed`, `enablePcap`, `enableCsvOutput`, `csvFilename` |

Run `./ns3 run "wifi-bf-network --PrintHelp"` for the full list with descriptions.

### Batch scenario runner

[`run_simulations.sh`](run_simulations.sh) automates four parameter sweeps (20 seeds each, run in parallel) that reproduce the trade-off studies discussed in the paper:

| Scenario | Script function | Fixed | Varied | Output CSV |
|---|---|---|---|---|
| 1 - Data + sensing integration | `scenario1` | interval = 307.2 ms, full sequence | `nStations` (1–10), `sensingDurationMs` (10.24/51.2/92.16) | `scenario1_data_sensing_integration.csv` |
| 2 - Multiple SMEs per SAW | `scenario2` | interval = 307.2 ms, 2 stations | `sensingDurationMs` (7 values), `sensingPhaseSequence` (3 sequences) | `scenario2_multiple_smes_per_saw.csv` |
| 3 - Throughput vs. sensing trade-off | `scenario3` | interval = 307.2 ms, 2 stations, full sequence | `sensingDurationMs` (7 values) + a sensing-disabled baseline | `scenario3_throughput_vs_sensing.csv` |
| 4 - Sensing interval impact | `scenario4` | duration = 51.2 ms, full sequence | `nStations` (1–10), `sensingIntervalMs` (102.4/307.2/512) | `scenario4_sensing_interval_impact.csv` |

Build ns-3 first (see [Build](#build)), then run:

```bash
./run_simulations.sh
```

By default it splits available CPUs evenly across the four scenarios (`JOBS_PER_SCENARIO = nproc / 4`), each writing its own CSV as runs complete.

### CSV output columns

Each row corresponds to one simulation run:

`Throughput_Mbps`, `AvgExchangesPerWindow`, `TotalSensingExchanges`, `TotalSAWs`, `SensingInterval_ms`, `SensingDuration_ms`, `SensingSequence`, `NumStations`, `NumAPs`, `SensingEnabled`, `Seed`, `SimulationTime_s`, `RatioStasSensing (not currently working)`

## Limitations

- **Frequency bands and procedure coverage** - Only Trigger-Based sensing for sub-7 GHz bands is covered; 60 GHz and Non-Trigger-Based procedures are out of scope.
- **Homogeneous STA participation** - All STAs currently perform both sensing and data communication; sensing/non-sensing STA coexistence in the same network is not yet supported.
- **Sensing session setup** - Session establishment (capability negotiation, SAW synchronization) is not implemented; operational parameters are fixed at simulation start instead.
- **Simulated CSI data** - CSI payloads are randomly generated, not derived from an actual channel/sensing model.

## Future Work

- Implement the sensing session establishment phase.
- Add support for non-sensing STAs coexisting with sensing STAs in the same IEEE 802.11bf network (`RatioStasSensing`).
- Integrate realistic CSI/channel models to enable actual sensing computation.

## Citation

If you use this module in your research, please cite:

```bibtex

```

## License

This software is licensed under the terms of the GNU GPLv2, same as ns-3. See [LICENSE](LICENSE) for details.
