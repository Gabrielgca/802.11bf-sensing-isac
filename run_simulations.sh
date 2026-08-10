#!/usr/bin/env bash
# =============================================================================
# run_simulations.sh — 802.11bf/ax coexistence simulation batch runner
# Parallelism: 4 scenarios run concurrently; within each scenario individual
# runs are also parallelised up to JOBS_PER_SCENARIO workers.
# =============================================================================

# ---------------------------------------------------------------------------
# Common parameters
# ---------------------------------------------------------------------------
SIM_TIME=10
SEEDS=(1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20)   # 20 seeds for better stats (adjust as needed)
STATIONS=(1 2 3 4 5)
DURATIONS=(10.24 51.2 92.16)   # sensingDurationMs ms (parsed as TU by uint64_t: 10→10.24ms, 51→52.2ms, 92→94.2ms)
INTERVALS=(102.4 307.2 512)    # sensingIntervalMs (ms)

COMMON="--simulationTime=${SIM_TIME} --nBss=1 --nBfBss=1 --sensingPriority=1 \
        --ratioStasSensing=1.0 --enablePcap=false --enableCsvOutput=true --enableFading=true"

NS3=./ns3

# CSV header must match exactly what wifi-bf-network.cc writes
CSV_HEADER="Throughput_Mbps,AvgExchangesPerWindow,TotalSensingExchanges,\
TotalSAWs,SensingInterval_ms,SensingDuration_ms,SensingSequence,\
NumStations,NumAPs,SensingEnabled,Seed,SimulationTime_s,RatioStasSensing"

# ---------------------------------------------------------------------------
# Job budget: split CPUs evenly across the 4 parallel scenarios
# ---------------------------------------------------------------------------
TOTAL_CPUS=$(nproc 2>/dev/null || echo 4)
JOBS_PER_SCENARIO=$(( TOTAL_CPUS / 4 ))
[ "${JOBS_PER_SCENARIO}" -lt 1 ] && JOBS_PER_SCENARIO=1

# Throttle background jobs within a scenario subshell.
# Uses only the calling subshell's job table, so each scenario is independent.
limit_jobs() {
    while [ "$(jobs -rp | wc -l)" -ge "${JOBS_PER_SCENARIO}" ]; do
        sleep 0.1
    done
}

echo "============================================================"
echo " 802.11bf simulation batch — $(date)"
echo " CPUs: ${TOTAL_CPUS}  |  jobs/scenario: ${JOBS_PER_SCENARIO}"
echo "============================================================"

# =============================================================================
# SCENARIO 1: Data + Sensing Integration
#   Fixed : sensingPhaseSequence, sensingIntervalMs = 307.2 ms
#   Varied: nStations (1-5), sensingDurationMs (10.24/51.2/92.16 ms)
# =============================================================================
scenario1() {
    local csv="scenario1_data_sensing_integration.csv"
    local seq="POLLING,NDPA_SOUNDING,TF_SOUNDING"
    local interval=307.2
    local stations=(1 2 3 4 5 6 7 8 9 10)

    echo "${CSV_HEADER}" > "${csv}"
    echo "[S1] started — writing to ${csv}"

    for SEED in "${SEEDS[@]}"; do
        for STA in "${stations[@]}"; do
            for DUR in "${DURATIONS[@]}"; do
                limit_jobs
                echo "  [S1] seed=${SEED} nStations=${STA} sensingDurationMs=${DUR}"
                $NS3 run "wifi-bf-network \
                    ${COMMON} \
                    --seed=${SEED} \
                    --nStations=${STA} \
                    --sensingIntervalMs=${interval} \
                    --sensingDurationMs=${DUR} \
                    --sensingPhaseSequence=${seq} \
                    --csvFilename=${csv}" 2>/dev/null &
            done
        done
    done
    wait
    echo "[S1] done"
}

# =============================================================================
# SCENARIO 2: Multiple SMEs in a SAW
#   Fixed : sensingIntervalMs = 307.2 ms, nStations = 2
#   Varied: sensingDurationMs (10.24/20.48/30.72/40.96/51.2/92.16/102.4 ms) + sensingPhaseSequence (3 sequences)
# =============================================================================
scenario2() {
    local csv="scenario2_multiple_smes_per_saw.csv"
    local interval=307.2
    local stations=2
    local seqs=(
        "POLLING,NDPA_SOUNDING"
        "POLLING,TF_SOUNDING"
        "POLLING,NDPA_SOUNDING,TF_SOUNDING"
    )
    local durations=(10.24 20.48 30.72 40.96 51.2 92.16 102.4)   # sensingDurationMs ms values

    echo "${CSV_HEADER}" > "${csv}"
    echo "[S2] started — writing to ${csv}"

    for SEED in "${SEEDS[@]}"; do
        for DUR in "${durations[@]}"; do
            for SEQ in "${seqs[@]}"; do
                limit_jobs
                echo "  [S2] seed=${SEED} sensingDurationMs=${DUR} seq=${SEQ}"
                $NS3 run "wifi-bf-network \
                    ${COMMON} \
                    --seed=${SEED} \
                    --nStations=${stations} \
                    --sensingIntervalMs=${interval} \
                    --sensingDurationMs=${DUR} \
                    --sensingPhaseSequence=${SEQ} \
                    --csvFilename=${csv}" 2>/dev/null &
            done
        done
    done
    wait
    echo "[S2] done"
}

# =============================================================================
# SCENARIO 3: Throughput vs Sensing Trade-off
#   Fixed : sensingIntervalMs = 307.2 ms, nStations = 2, sequence = full
#   Varied: sensingDurationMs (10.24/20.48/30.72/40.96/51.2/92.16/102.4 ms) + sensingPhaseSequence (3 sequences) + sensing-disabled baseline
# =============================================================================
scenario3() {
    local csv="scenario3_throughput_vs_sensing.csv"
    local interval=307.2
    local stations=2
    local seq="POLLING,NDPA_SOUNDING,TF_SOUNDING"
    local durations=(10.24 20.48 30.72 40.96 51.2 92.16 102.4)   # sensingDurationMs ms values

    echo "${CSV_HEADER}" > "${csv}"
    echo "[S3] started — writing to ${csv}"

    for SEED in "${SEEDS[@]}"; do
        # Sensing-disabled baseline
        limit_jobs
        echo "  [S3] seed=${SEED} sensing=DISABLED"
        $NS3 run "wifi-bf-network \
            ${COMMON} \
            --seed=${SEED} \
            --nStations=${stations} \
            --sensingIntervalMs=${interval} \
            --sensingDurationMs=51.2 \
            --sensingPhaseSequence=${seq} \
            --enableWiFiSensing=false \
            --csvFilename=${csv}" 2>/dev/null &

        # Sensing enabled with varying duration
        for DUR in "${durations[@]}"; do
            limit_jobs
            echo "  [S3] seed=${SEED} sensingDurationMs=${DUR} sensing=ENABLED"
            $NS3 run "wifi-bf-network \
                ${COMMON} \
                --seed=${SEED} \
                --nStations=${stations} \
                --sensingIntervalMs=${interval} \
                --sensingDurationMs=${DUR} \
                --sensingPhaseSequence=${seq} \
                --enableWiFiSensing=true \
                --csvFilename=${csv}" 2>/dev/null &
        done
    done
    wait
    echo "[S3] done"
}

# =============================================================================
# SCENARIO 4: Impact of Sensing Interval on Throughput
#   Fixed : sensingDurationMs = 51.2 ms, sequence = full
#   Varied: nStations (1-5), sensingIntervalMs (102.4/307.2/512 ms)
# =============================================================================
scenario4() {
    local csv="scenario4_sensing_interval_impact.csv"
    local duration=51.2   # 51.2 ms
    local seq="POLLING,NDPA_SOUNDING,TF_SOUNDING"
    local stations=(1 2 3 4 5 6 7 8 9 10)

    echo "${CSV_HEADER}" > "${csv}"
    echo "[S4] started — writing to ${csv}"

    for SEED in "${SEEDS[@]}"; do
        for STA in "${stations[@]}"; do
            for INTV in "${INTERVALS[@]}"; do
                limit_jobs
                echo "  [S4] seed=${SEED} nStations=${STA} sensingIntervalMs=${INTV}"
                $NS3 run "wifi-bf-network \
                    ${COMMON} \
                    --seed=${SEED} \
                    --nStations=${STA} \
                    --sensingIntervalMs=${INTV} \
                    --sensingDurationMs=${duration} \
                    --sensingPhaseSequence=${seq} \
                    --csvFilename=${csv}" 2>/dev/null &
            done
        done
    done
    wait
    echo "[S4] done"
}

# ---------------------------------------------------------------------------
# Launch all 4 scenarios in parallel, wait for all to finish
# ---------------------------------------------------------------------------
scenario1 &
scenario2 &
scenario3 &
scenario4 &
wait

echo ""
echo "============================================================"
echo " All scenarios complete — $(date)"
echo " Results:"
echo "   scenario1_data_sensing_integration.csv"
echo "   scenario2_multiple_smes_per_saw.csv"
echo "   scenario3_throughput_vs_sensing.csv"
echo "   scenario4_sensing_interval_impact.csv"
echo "============================================================"
