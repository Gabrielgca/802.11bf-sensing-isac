#!/bin/bash

export IDO_LOG_INFO="ApWifiMac=level_info|prefix_time:StaWifiMac=level_info|prefix_time:ChannelAccessManager=level_info|prefix_time:FrameExchangeManager=level_info|prefix_time:HeFrameExchangeManager=level_info|prefix_time:InfrastructureWifiMac=level_info|prefix_time:WifiPhy=level_info|prefix_time"
export IDO_LOG_FUNCTION="ApWifiMac=level_function|prefix_time:StaWifiMac=level_function|prefix_time:Txop=level_function|prefix_time:ChannelAccessManager=level_function|prefix_time:FrameExchangeManager=level_function|prefix_time:HeFrameExchangeManager=level_function|prefix_time:InfrastructureWifiMac=level_function|prefix_time:WifiPhy=level_info|prefix_time:ChannelSounding=level_function|prefix_time:MacRxMiddle=level_function|prefix_time:QosTxop=level_function|prefix_time:QosFrameExchangeManager=level_function|prefix_time:RrMultiUserScheduler=level_function|prefix_time:MultiUserScheduler=level_function|prefix_time"

################################################################################################
#                                 Setting testing varibles                                     #
################################################################################################
# defined test variables
#seed=("347" "722" "876" "59" "463" "389" "815" "213" "630" "498" "174" "954" "320" "282" "791" "637" "450" "198" "903" "577" "764" "232" "344" "982" "615" "79" "829" "201" "941" "705" "364" "628" "110" "543" "976" "47" "489" "357" "698" "255" "921" "402" "676" "123" "861" "534" "777" "239" "997" "582"
#"367" "153" "802" "438" "294" "631" "580" "824" "139" "670" "911" "276" "48" "501" "112" "703" "980" "405" "762" "391" "17" "708" "242" "886" "56" "604" "755" "669" "318" "459" "821" "115" "497" "239" "711" "841" "692" "303" "950" "638" "553" "764" "406" "929" "871" "611" "473" "109" "921" "324") # 100 seeds
# seed=("347" "722" "876" "59" "463" "389" "815" "213" "630" "498" "174" "954" "320" "282" "791" "637" "450" "198" "903" "577" "764" "232" "344" "982" "615" "79" "829" "201" "941" "705" "364" "628" "110" "543" "976" "47" "489" "357" "698" "255" "921" "402" "676" "123" "861" "534" "777" "239" "997" "582") # 50 seed values
# seed=("347" "722" "876" "59" "463" "389" "815" "213" "630" "498" "174" "954" "320" "282" "791" "637" "450" "198" "903" "577" "764" "232" "344" "79" "829") # 25 seed values
# seed=("347" "722" "876" "59" "463" "389" "815" "213" "630" "498") # 10 seed values
# seed=("347" "722" "876" "59") # 4 seed values
seed=("347") # 1 seed values
sensingIntervalMs=("50" "40" "30" "20" "10") # 5 intervals
radius=("1" "2" "3" "4" "5" "10" "15" "20" "25" "30") # 10 radius
distance=("5" "10" "15" "20" "25" "30")
frequency=("5" "6")
mcs=("0" "1" "2" "3" "4" "5" "6" "7" "8" "9" "10" "11")
nBss=("2" "3" "4" "5" "6")                      # 5 BSS
nBssMulSta=("2" "3" "4" "5" "6")                # 5 BSS
nStations=("1" "2" "3" "4" "5" "6" "7" "8" "9") # 9 stations
nStationsMultiBss=("1" "2" "3" "4")             # 4 stations
multipleBss=("true" "false")
sensingDurationMs=("1" "5" "10" "15" "20" "25" "50" "100")
numerator=("0" "1" "2" "3" "4" "5" "6" "7" "8" "9" "10" "11") # 12 numerator
senPriority=("0" "1" "2" "3")                                 # 4 sensing priority
residntialDensity=("0" "1")
Bandwidth=("20" "40" "80" "160")
size=${#seed[@]}

# Change / Fill this variable for the save location in local ! 
export Save_loc="/home/gabriel/Documents/Independent_Study/coexistence-of-802.11bf-and-802.11ax"

################################################################################################
#                          Run the program with different variable                             #
################################################################################################
option=$1

##                                                    ##
####            change ns3 build mode                ###
##                                                    ##
if [ "$option" == "build" ]; then
    build_mode=$2
    if [ "$build_mode" == "debug" ]; then
        # ./ns3 clean
        ./ns3 configure --build-profile=debug --enable-examples --enable-tests --disable-werror
        ./ns3 build
    elif [ "$build_mode" == "optimized" ]; then
        ./ns3 clean
        ./ns3 configure --build-profile=optimized --enable-examples --enable-tests --disable-werror
        ./ns3 build
    fi
fi

if [ "$option" == "run" ]; then
    ./ns3
fi

##                                                    ##
####     immediate testing and result logging        ###
##                                                    ##
if [ "$option" == "quick" ]; then
    logging=$2
    if [ "$logging" == "no_log" ]; then
        ./ns3 run examples/wireless/wifi-bf-network.cc --no-build
    elif [ "$logging" == "log_info" ]; then
        (./ns3 run examples/wireless/wifi-bf-network.cc) &
        NS_LOG=$IDO_LOG_INFO ./ns3 run examples/wireless/wifi-bf-network.cc --no-build >log.txt 2>&1
        wait
    elif [ "$logging" == "log_function" ]; then
        (./ns3 run examples/wireless/wifi-bf-network.cc) &
        (NS_LOG=$IDO_LOG_INFO ./ns3 run examples/wireless/wifi-bf-network.cc --no-build >log.out 2>&1) &
        NS_LOG=$IDO_LOG_FUNCTION ./ns3 run examples/wireless/wifi-bf-network.cc --no-build >log1.out 2>&1
        wait
    fi
fi

##                                                    ##
####   investigate the influence of each parameter   ###
##                                                    ##
if [ "$option" == "investigate" ]; then
    logging=$2
    argument=$3
    namefile=$4
    if [ "$logging" == "no_log" ]; then
        ./ns3 run "examples/wireless/wifi-bf-network.cc $argument" 
    elif [ "$logging" == "log_info" ]; then
        (./ns3 run "examples/wireless/wifi-bf-network.cc $argument") &
        NS_LOG=$IDO_LOG_INFO ./ns3 run "examples/wireless/wifi-bf-network.cc $argument" --no-build >log$namefile.txt 2>&1
        wait
    elif [ "$logging" == "log_function" ]; then
        (./ns3 run "examples/wireless/wifi-bf-network.cc $argument") &
        (NS_LOG=$IDO_LOG_INFO ./ns3 run "examples/wireless/wifi-bf-network.cc $argument" --no-build >log_info$namefile.txt 2>&1) &
        NS_LOG=$IDO_LOG_FUNCTION ./ns3 run "examples/wireless/wifi-bf-network.cc $argument" --no-build >log_func$namefile.txt 2>&1
        wait
    fi
fi

if [ "$option" == "debug" ]; then
    argument=$2
    ./ns3 run --gdb "examples/wireless/wifi-bf-network.cc $argument"
fi

# Main loop
# ( NS_LOG=$IDO_LOG_INFO ./ns3 run "examples/wireless/wifi-bf-network.cc --frequency=5 --nStations=4 --seed=18 --mcs=6 --multipleBss=False" > log_singleBSS.out 2>&1 ) &
# NS_LOG=$IDO_LOG_INFO ./ns3 run "examples/wireless/wifi-bf-network.cc --frequency=5 --nStations=4 --seed=18 --mcs=6 --multipleBss=True" > log_multipleBSS.out 2>&1
# wait

##                                                    ##
####        Testing variable : multipleBss           ###
##                                                    ##
if [ "$option" == "multipleBssAx" ]; then
    counter=0
    for BssNum in "${nBss[@]}"; do
        for s in "${seed[@]}"; do
            # Total process : 25 * 6 * 5 = 350
            nAxBss=$((BssNum - 1))
            echo "Processing file $counter: nBss = $BssNum, Seed = $s"

            # Run each iteration in the background for parallel processing
            (
                ./ns3 run "examples/wireless/wifi-bf-network.cc --nBss=$BssNum --nAxBss=$nAxBss --seed=$s --sensingIntervalMs=1000 --simulationTime=10.0" > /$Save_loc/Result/Result_multipleBSS/MultipleAX/"nBss=$BssNum"/nolog_multipleBss=$BssNum,seed=$s.txt 2>&1

            ) &

            ((counter++))

            # Limit the number of parallel processes (adjust as needed)
            if ((counter % 20 == 0)); then
                wait
            fi

        done
    done
fi
# ./ns3 run "examples/wireless/wifi-bf-network.cc --nBss=2 --nAxBss=1 --nStations=4 --ratioStasSensing=0.5 --sensingPriority=1 --seed=347 --scenario=6 --simulationTime=10.0" 
# nBss=("4")
if [ "$option" == "multipleBssBf" ]; then
    counter=0
    for BssNum in "${nBss[@]}"; do
        for s in "${seed[@]}"; do
            echo "Processing file $counter: nBss = $BssNum, Seed = $s"

            # Run each iteration in the background for parallel processing
            (
                ./ns3 run "examples/wireless/wifi-bf-network.cc --nBss=$BssNum --nAxBss=1 --seed=$s --simulationTime=10.0" >/$Save_loc/Result/Result_multipleBSS/MultipleBF/"nBss=$BssNum"/nolog_multipleBss=$BssNum,seed=$s.out 2>&1

            ) &

            ((counter++))

            # Limit the number of parallel processes (adjust as needed)
            if ((counter % 20 == 0)); then
                wait
            fi

        done

    done
fi

if [ "$option" == "multipleBssResidential" ]; then
    counter=0
    for dense in "${residntialDensity[@]}"; do
        for s in "${seed[@]}"; do
            echo "Processing file $counter: dense = $dense, Seed = $s"

            # Run each iteration in the background for parallel processing
            (
                # Example of running your program with parameterss
                ./ns3 run "examples/wireless/wifi-bf-network.cc --residentialDensity=$dense --scenario=3 --seed=$s --simulationTime=10.0" >/$Save_loc/Result/Result_multipleBSS/Residential/"dense=$dense"/Result/nolog_dense=$dense,seed=$s.out 2>&1

            ) &

            ((counter++))

            # Limit the number of parallel processes (adjust as needed)
            if ((counter % 4 == 0)); then
                wait
            fi
        done
    done
fi

if [ "$option" == "multipleBssBfmulSta" ]; then
    counter=0
    for nSta in "${nStationsMultiBss[@]}"; do
        for BssNum in "${nBssMulSta[@]}"; do
            for s in "${seed[@]}"; do
                echo "Processing file $counter: nSta=$nSta, nBss = $BssNum, Seed = $s"

                # Run each iteration in the background for parallel processing
                (
                    forRatio=$((2 * BssNum))
                    ./ns3 run "examples/wireless/wifi-bf-network.cc --nBss=$forRatio --nAxBss=$BssNum --nStations=$nSta --seed=$s " >/$Save_loc/Result/Result_multipleBSS/MultipleRatioMultipleSta/"sta=$nSta"/"nBss=$BssNum"/nolog_multipleBss=$BssNum,seed=$s.out 2>&1
                ) &

                ((counter++))

                # Limit the number of parallel processes (adjust as needed)
                if ((counter % 4 == 0)); then
                    wait
                fi

            done
        done
    done
fi

if [ "$option" == "multipleBssRatio" ]; then
    BssNum=("12")
    counter=0
    for numer in "${numerator[@]}"; do
        for s in "${seed[@]}"; do
            nAxBss=$((BssNum - 1))
            echo "Processing file $counter: nBss = $BssNum, Seed = $s"

            # Run each iteration in the background for parallel processing
            (
                ./ns3 run "examples/wireless/wifi-bf-network.cc --nBss=$BssNum --numerator=$numer --seed=$s" >/$Save_loc/Result/Result_multipleBSS/Ratio/"ratio=$numer""to12"/nolog_multipleBss=seed=$s.out 2>&1

            ) &

            ((counter++))

            # Limit the number of parallel processes (adjust as needed)
            if ((counter % 4 == 0)); then
                wait
            fi

        done
    done
fi

if [ "$option" == "multipleBssBfInterval" ]; then
    counter=0
    for Interval in "${sensingIntervalMs[@]}"; do
        for BssNum in "${nBssMulSta[@]}"; do
            for s in "${seed[@]}"; do
                echo "Processing file $counter: Interval=$Interval, nBss = $BssNum, Seed = $s"

                # Run each iteration in the background for parallel processing
                (
                    ./ns3 run "examples/wireless/wifi-bf-network.cc --nBss=$BssNum --nAxBss=1 --simulationTime=$Interval --sensingIntervalMs=$Interval --seed=$s" --no-build >/$Save_loc/Result/Result_multipleBSS/multipleBFInterval/"Interval=$Interval"/"nBss=$BssNum"/nolog_multipleBss=$BssNum,seed=$s.out 2>&1
                ) &

                ((counter++))

                # Limit the number of parallel processes (adjust as needed)
                if ((counter % 4 == 0)); then
                    wait
                fi

            done
        done
    done
fi

if [ "$option" == "multipleBssBfIntervalOffice" ]; then
    counter=0
    for Interval in "${sensingIntervalMs[@]}"; do
        for s in "${seed[@]}"; do
            echo "Processing file $counter: Interval=$Interval, Seed = $s"

            # Run each iteration in the background for parallel processing
            (
                ./ns3 run "examples/wireless/wifi-bf-network.cc --scenario=5 --nBfBss=6 --simulationTime=60.0 --sensingIntervalMs=$Interval --seed=$s" --no-build >$Save_loc/Result/Result_multipleBSS/multipleBFInterval/"Interval=$Interval"/nolog_multipleBss_seed=$s.out 2>&1
            ) &
 # ./ns3 run "examples/wireless/wifi-bf-network.cc --scenario=5 --nBfBss=5 --nAxBss=1 --sensingIntervalMs=10 --seed=347 --simulationTime=3.0"
            ((counter++))

            # Limit the number of parallel processes (adjust as needed)
            if ((counter % 4 == 0)); then
                wait
            fi

        done
    done
fi

##                                                    ##
####  Testing variable : sensing priority            ###
##                                                    ##
if [ "$option" == "idealSensPrioScenario" ]; then
    counter=0
    for prio in "${senPriority[@]}"; do
        for nseed in "${seed[@]}"; do
            echo "Processing file $counter: prio = $prio, Seed = $nseed"

            # Run each iteration in the background for parallel processing
            (
                # Example of running your program with parameters
                ./ns3 run "examples/wireless/wifi-bf-network.cc --sensingPriority=$prio --seed=$nseed --nBss=2 --nAxBss=1 --simulationTime=60.0" >/$Save_loc/Result/Result_sensPrio/Result_sensPrio_Ideal/"prio=$prio"/nolog_priority=seed=$nseed.out 2>&1

            ) &
            
            ((counter++))

            # Limit the number of parallel processes (adjust as needed)
            if ((counter % 4 == 0)); then
                wait
            fi
        done
    done
fi

##                                                    ##
####  Testing variable : sensing priority            ###
##                                                    ##
if [ "$option" == "simpleSensingIntervals" ]; then
    counter=0
    for Interval in "${simpleSensingIntervals[@]}"; do
        for nseed in "${seed[@]}"; do
            echo "Processing file $counter: Interval = $Interval, Seed = $nseed"

            # Run each iteration in the background for parallel processing
            (
                # Example of running your program with parameters
                ./ns3 run "examples/wireless/wifi-bf-network.cc --sensingIntervalMs=$Interval --seed=$nseed --nBss=2 --nAxBss=1 --simulationTime=60.0" >/$Save_loc/Result/Result_sensInterval/"Interval=$Interval"/nolog_priority=seed=$nseed.out 2>&1

            ) &
            
            ((counter++))

            # # Limit the number of parallel processes (adjust as needed)
            if ((counter % 4 == 0)); then
                wait
            fi
        done
    done
fi

##                                                    ##
####  Testing variable : residential scenario        ###
##                                                    ##
if [ "$option" == "residentialSensPrioScenario" ]; then
    counter=0
    for prio in "${senPriority[@]}"; do
        for nseed in "${seed[@]}"; do
            echo "Processing file $counter: prio = $prio, Seed = $nseed"

            # Run each iteration in the background for parallel processing
            (
                # Example of running your program with parameters
                ./ns3 run "examples/wireless/wifi-bf-network.cc --sensingPriority=$prio --seed=$nseed --scenario=3 --simulationTime=60.0" >/$Save_loc/Result/Result_sensPrio/Result_sensPrio_Residential/nolog_priority_prio$prio\_=seed=$nseed.out 2>&1

            ) &
            
            ((counter++))

            # Limit the number of parallel processes (adjust as needed)
            if ((counter % 4 == 0)); then
                wait
            fi
        done
    done
fi


if [ "$option" == "volumetricVideoStreamingScenario" ]; then
    counter=0
    for prio in "${senPriority[@]}"; do
        for nseed in "${seed[@]}"; do
            echo "Processing file $counter: prio = $prio, Seed = $nseed"

            # Run each iteration in the background for parallel processing
            (
                # Example of running your program with parameters
                ./ns3 run "examples/wireless/wifi-bf-network.cc --nBss=1 --nStations=10 --ratioStasSensing=0.5 --sensingPriority=$prio --seed=$nseed --scenario=6 --simulationTime=60.0" >/$Save_loc/Result/Result_VVsens/Result_VVsens_Residential/nolog_priority_prio_$prio\_seed=$nseed.out 2>&1

            ) 
            
            # ((counter++))

            # # Limit the number of parallel processes (adjust as needed)
            # if ((counter % 4 == 0)); then
            #     wait
            # fi
        done
    done
fi


# NS_LOG="ApWifiMac=level_info|prefix_time:StaWifiMac=level_info|prefix_time:ChannelAccessManager=level_info|prefix_time:FrameExchangeManager=level_info|prefix_time:HeFrameExchangeManager=level_info|prefix_time:InfrastructureWifiMac=level_info|prefix_time:WifiPhy=level_info|prefix_time" ./ns3 run "examples/wireless/wifi-bf-network.cc --nBss=2 --nAxBss=1 --nStations=4 --ratioStasSensing=0.5 --sensingPriority=1 --seed=347 --scenario=6 --simulationTime=3.0" > log.out 2>&1

# ./ns3 run "examples/wireless/wifi-bf-network.cc --nBss=1 --nBfBss=1 --nStations=2 --ratioStasSensing=1 --sensingPriority=1 --seed=347 --scenario=6 --simulationTime=3.0" > log2.out 2>&1