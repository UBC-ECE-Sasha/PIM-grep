#!/bin/bash

search="unsigned"
where="/scratch/linux-stable"

tasklets_set="1 2 4 6 8 10 12 14 16 18 20 22"
files_per_dpu="1 2 4 8 16 32 64 128"
ranks_set="1 2 3 4 8"
#tasklets_set="14 16"


for files in $files_per_dpu; do

	# compile the different verions
	for tasklets in $tasklets_set; do
		make NR_TASKLETS=$tasklets MAX_FILES_PER_DPU=$files
	done

	for tasklets in $tasklets_set; do
		echo "Tasklets: $tasklets"
		for ranks in $ranks_set; do
			total_time=$(time find $where | ./host-$tasklets -c -d -r $ranks -t "$search" - > output-T$tasklets-F$files-R$ranks.dpu)
			echo "Time: $total_time"
		done
	done
done
