#!/usr/bin/env python
#
# swdiag-server module for monitoring the memory of a server. This is important since
# if the system starts swapping or runs out of memory the performance of the entire 
# system is impacted, and therefore a number of other modules will depend on this one.

from optparse import OptionParser

import diag_memory_conf

parser = OptionParser()
parser.add_option("-c", "--conf", action="store_true", dest="conf", help="request configuration", default=False)
parser.add_option("-t", "--test", action="store", type="string", dest="testname", help="perform a test", default=None)
parser.add_option("-i", "--instance", action="store", type="string", dest="instancename", help="perform a test on a particular instance", default=None)

(options, args) = parser.parse_args()

def getMemInfo():
    allInfo = {}
    f = open("/proc/meminfo", "rb")
    lines = f.readlines()
    del f
    lines = map(lambda x: x.strip().split(), lines)
    
    for line in lines:
        type = line[0]
        # we only accept lines that have colons after labels
        if type.endswith(":"):
            allInfo[type[:-1]] = int(line[1])
    allInfo["ActualFree"] = allInfo["MemFree"] + allInfo["Buffers"] + allInfo["Cached"]
    allInfo["SwapUsed"] = allInfo["SwapTotal"] - allInfo["SwapFree"]
    allInfo["ActualFreePercent"] = 100.0 / allInfo["MemTotal"] * allInfo["ActualFree"]
    return allInfo  
  
def main():
    if options.conf:
        print '"comp":{"name":"Memory"},'
        print '"test":{"name":"memory_poll_meminfo",'
        print '        "polled":true,'
        print '        "interval":"fast",'
        print '        "comp":"Memory"},'
        
        print '"test":{"name":"memory_free_percent",'
        print '        "comp":"Memory"},'
        print '"rule":{"name":"memory_free_percent_thresh",'
        print '        "input":"memory_free_percent",'
        print '        "operator":"SWDIAG_RULE_LESS_THAN_N",'
        print '        "n":', diag_memory_conf.free_percent_threshold, ','
        print '        "comp":"Memory"},'
        print '"rule":{"name":"memory_free_percent_thresh_time",'
        print '        "input":"memory_free_percent_thresh",'
        print '        "action":"memory_free_notification",'
        print '        "operator":"SWDIAG_RULE_N_IN_M",'
        print '        "n":',diag_memory_conf.free_percent_n,',"m":',diag_memory_conf.free_percent_m, ','
        print '        "comp":"Memory"},'
        print '"email":{"name":"memory_free_notification",'
        print '         "subject":"Free memory has dropped under', diag_memory_conf.free_percent_threshold, '%. Performance will be impacted."},'
        
        print '"test":{"name":"memory_swap_inuse",'
        print '        "comp":"Memory"},'
        print '"rule":{"name":"memory_swap_inuse_thresh",'
        print '        "input":"memory_swap_inuse",'
        print '        "operator":"SWDIAG_RULE_GREATER_THAN_N",'
        print '        "n":',diag_memory_conf.swap_inuse_threshold, ','
        print '        "comp":"Memory"},'
        print '"rule":{"name":"memory_swap_inuse_thresh_time",'
        print '        "input":"memory_swap_inuse_thresh",'
        print '        "action":"memory_swap_inuse_notification",'
        print '        "operator":"SWDIAG_RULE_N_IN_M",'
        print '        "n":',diag_memory_conf.swap_inuse_n,',"m":',diag_memory_conf.swap_inuse_m, ','
        print '        "comp":"Memory"},'
        print '"email":{"name":"memory_swap_inuse_notification",'
        print '         "subject":"Swap is now consistently greater than',diag_memory_conf.swap_inuse_threshold, 'Kb ."},'

        print '"ready":["memory_poll_meminfo", "memory_free_percent", "memory_swap_inuse"]'
        print ''
    else:
        if options.testname:
            if options.testname == 'memory_poll_meminfo':
                memInfo = getMemInfo()
                print '"result":{"test", "memory_free_percent",'
                print '          "value",' '%d' % memInfo["ActualFreePercent"], "}"
                print '"result":{"test", "memory_swap_inuse",'
                print '          "value",' '%d' % memInfo["SwapUsed"], "}"
                print '"result":{"test", "memory_poll_meminfo",'
                print '          "result", "ignore"}'
                
        if options.instancename:
            print "Instance", options.instancename

main()