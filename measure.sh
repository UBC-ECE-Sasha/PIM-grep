#!/bin/bash

# CSV Format:
#version,time,parallelism
#host,4.361,0
#ranks=1,39.76,1
#ranks=1,20.24,2
#ranks=1,10.48,4
#ranks=1,7.23,6
#...
#ranks=2,2.81,18
#ranks=2,2.88,20
#ranks=2,2.86,22

# the DPU output files are named like this:
# output-T<tasklets>-F<files>-R<ranks>.dpu

timestamp=$(date +%F)
search="unsigned"
where="/scratch/linux-stable"
outfile="grep.csv"

files_per_dpu="64 128 256"
ranks_set="1 2 3"
tasklets_set="1 2 4 6 8 10 12 14 16 18 20"

# run grep twice on the host - it runs faster the second time
#cmd="time grep -R -c 'unsigned' /scratch/linux-stable > output.host"
#echo $cmd
#$cmd
#cmd="time grep -R -c 'unsigned' /scratch/linux-stable >> output.host"
#echo $cmd
#$cmd

for files in $files_per_dpu; do

	# compile the different verions
	for tasklets in $tasklets_set; do
		make NR_TASKLETS=$tasklets MAX_FILES_PER_DPU=$files
	done

	for ranks in $ranks_set; do
		for tasklets in $tasklets_set; do
			echo "Files: $files Ranks: $ranks Tasklets: $tasklets"
			total_time=$(time find $where | ./host-$tasklets -c -d -r $ranks -t "$search" - > output-T$tasklets-F$files-R$ranks.dpu)
		done
	done
done

# write the header (and overwrite any existing file contents)
echo "version,time,parallelism" > $outfile

# next comes the host results
logfile="output.host"
grep "Total time" $logfile
echo "host,XXX,0" >> $outfile

for files in $files_per_dpu; do
for ranks in $ranks_set; do
	for tasklets in $tasklets_set; do
		logfile="output-T$tasklets-F$files-R$ranks.dpu"
		tt=$(grep "Total time" $logfile | awk '{print $3+0}') # adding 0 forces string to int
		echo $logfile
		echo "ranks=$ranks,$tt,$tasklets" >> $outfile
	done
done
done

# create an archive of the data files
tar cjf $timestamp.results.tar.bz2 *.dpu output.host
echo "results are in $timestamp.results.tar.bz2"
