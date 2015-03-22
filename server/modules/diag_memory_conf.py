#
# Configuration for diag_memory.py
#
# Threshold for when we start taking notw of the amount of free memory remaining
# and also the number of times N that the threshold must be exceeded in M tests
# before we notify the user that there is an issue.
#
free_percent_threshold=10
free_percent_n=3
free_percent_m=5

#
# Threshold in Kb over which we will start taking note of the amount of swap being
# used. 
swap_inuse_threshold=500000
swap_inuse_n=10
swap_inuse_m=15
