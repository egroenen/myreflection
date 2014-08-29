#!/usr/bin/env python
#
# swdiag-server module for postgres processes. We monitor the postmaster processes
# (which we store the identity on disk to detect changes) and report on any postmaster
# processes that are looking suspect, for example being idle in transaction for extended
# periods.
#
# The way it works is it has one master polled test 'postgres_poll_processes' that swdiag
# calls every so often. The first time around (when the conf is called, an instance of
# each notification test is created for that pid. Those instances are subsequently updated
# with their test status. Where new postgres processes are created a new instance will be 
# created, and where one is removed, that instance will also be removed.
#
# Because this script is only run periodically it must store on disk a list of all of the 
# instances that it has created in swdiag in the past.
#
#

from optparse import OptionParser

import pickle
import os.path
import diag_postgres_conf

parser = OptionParser()
parser.add_option("-c", "--conf", action="store_true", dest="conf", help="request configuration", default=False)
parser.add_option("-t", "--test", action="store", type="string", dest="testname", help="perform a test", default=None)
parser.add_option("-i", "--instance", action="store", type="string", dest="instancename", help="perform a test on a particular instance", default=None)

(options, args) = parser.parse_args()

pickle_filename='/var/tmp/diag_pg_proc_pkl'
def loadPostgresProcesses():
    pids=[pid for pid in os.listdir('/proc') if pid.isdigit()]
    pg_pids = {}
    for pid in pids:
        try:
            cmdline = open(os.path.join('/proc', pid, 'cmdline'), 'rb').read()
            if cmdline.startswith("postgres:"):
                pg_pids[pid] = cmdline
        except IOError:
            pass
        
    return pg_pids
    
def main():
    if options.conf:
        try:
            os.remove(pickle_filename)
        except OSError:
            pass
        # Can't use a structure and json libs 'cos they are not in python 2.4
        print '''[{"comp":{"name":"Postgres"}},
                {"test":{"name":"pg_poll_processes",
                        "polled":true,
                        "interval":"fast",
                        "comp":"Postgres"}},
                {"test":{"name":"pg_idle_in_trans",
                        "comp":"Postgres"}},
                {"rule":{"name":"pg_idle_in_trans_rule",
                        "input":"pg_idle_in_trans",
                        "severity":"SWDIAG_SEVERITY_HIGH",
                        "action":"pg_idle_in_trans_notification",
                        "operator":"SWDIAG_RULE_N_IN_ROW",
                        "n":''', diag_postgres_conf.idle_n, ''',
                        "comp":"Postgres"}},
                {"email":{"name":"pg_idle_in_trans_notification",
                         "subject":"Postgresql process has been in the state 'idle in transaction' for a long time",
                         "command","ps -ef | grep 'postgres:'"}},
                         
                {"test":{"name":"pg_idle_proc_count",
                         "comp":"Postgres"}},
                {"rule":{"name":"pg_idle_proc_count_w_rule",
                         "input":"pg_idle_proc_count",
                         "severity":"SWDIAG_SEVERITY_MEDIUM",
                         "operator":"SWDIAG_RULE_GREATER_THAN_N",
                         "n":''', diag_postgres_conf.warning_pg_idle_count, ''',
                         "comp":"Postgres"}},
                {"rule":{"name":"pg_idle_proc_count_c_rule",
                         "input":"pg_idle_proc_count",
                         "action":"pg_idle_proc_count_notification",
                         "severity":"SWDIAG_SEVERITY_HIGH",
                         "operator":"SWDIAG_RULE_GREATER_THAN_N",
                         "n":''', diag_postgres_conf.critical_pg_idle_count, ''',
                         "comp":"Postgres"}},
                {"email":{"name":"pg_idle_proc_count_notification",
                         "subject":"Postgresql idle process count critical",
                         "command","echo -n 'Process Count: ~'; ps -ef | grep ' postgres:' | grep 'idle in transaction' | wc -l; ps -ef | grep 'postgres:'"}},

                {"test":{"name":"pg_proc_count",
                         "comp":"Postgres"}},
                {"rule":{"name":"pg_proc_count_1_rule",
                         "input":"pg_proc_count",
                         "severity":"SWDIAG_SEVERITY_LOW",
                         "operator":"SWDIAG_RULE_GREATER_THAN_N",
                         "n":''', diag_postgres_conf.warning_pg_count, ''',
                         "comp":"Postgres"}},
                {"rule":{"name":"pg_proc_count_2_rule",
                         "input":"pg_proc_count",
                         "action":"pg_proc_count_notification",
                         "severity":"SWDIAG_SEVERITY_MEDIUM",
                         "operator":"SWDIAG_RULE_GREATER_THAN_N",
                         "n":''', diag_postgres_conf.critical_pg_count, ''',
                         "comp":"Postgres"}},
                {"email":{"name":"pg_proc_count_notification",
                         "subject":"Postgresql process count critical",
                         "command","echo -n 'Process Count: ~'; ps -ef | grep ' postgres:' | wc -l; ps -ef | grep 'postgres:'"}},
 
                {"test":{"name":"pg_health_test",
                         "health":"Postgres",
                         "comp":"Postgres"}},
                {"rule":{"name":"pg_health_threshold",
                         "input":"pg_health_test",
                         "severity":"SWDIAG_SEVERITY_NONE",
                         "operator":"SWDIAG_RULE_LESS_THAN_N",
                         "n":''', diag_postgres_conf.critical_health, ''',
                         "comp", "Postgres"}},
                {"rule":{"name":"pg_health_threshold_time",
                         "input":"pg_health_threshold",
                         "action":"pg_health_notification",
                         "severity":"SWDIAG_SEVERITY_NONE",
                         "operator":"SWDIAG_RULE_N_IN_ROW",
                         "n":''', diag_postgres_conf.health_time_n, ''',
                         "comp", "Postgres"}},
                
                {"email":{"name":"pg_health_notification",
                         "subject":"Postgresql health consistently under threshold ''', diag_postgres_conf.critical_health/10.0, '''%",
                         "command","echo -n 'Process Count: ~'; ps -ef | grep ' postgres:' | wc -l; ps -ef | grep 'postgres:'"}},
            
            '''
        
        # Now create the instances
        pg_procs = loadPostgresProcesses()
        for key in pg_procs:
            print '''{"instance":{"object":"pg_idle_in_trans",
                                  "name":"pid_''' + str(key) + '''"},
                         "instance":{"object":"pg_idle_in_trans_rule",
                                     "name":"pid_''' + str(key) + '''"}}''';

        print '{"ready":["pg_poll_processes", "pg_idle_in_trans", "pg_proc_count", "pg_idle_proc_count", "pg_health_test"]}]'
    else:
        if options.testname:
            if options.testname == 'pg_poll_processes':
                pg_procs_current = loadPostgresProcesses()
                # Read in the old values and reconcile the instances
                if os.path.exists(pickle_filename):
                    try:
                        pkl_file = open(pickle_filename, 'rb')
                        pg_procs_old = pickle.load(pkl_file)
                        pkl_file.close()
                        # Look for new ones
                        for key in pg_procs_current:
                            if pg_procs_old.get(key) == None:
                                print '''{"instance":{"object":"pg_idle_in_trans",
                                          "name":"pid_''' + str(key) + '''"},
                                          "instance":{"object":"pg_idle_in_trans_rule",
                                             "name":"pid_''' + str(key) + '''"}}''';
                            
                        # Look for dead ones
                        for key in pg_procs_old:
                            if pg_procs_current.get(key) == None:
                                print '''{"instance":{"object":"pg_idle_in_trans",
                                          "name":"pid_''' + str(key) + '''",
                                          "delete":true},
                                          "instance":{"object":"pg_idle_in_trans_rule",
                                             "name":"pid_''' + str(key) + '''",
                                             "delete":true}}'''
                    except:
                        print ''
                                     
                # Write existing ones out for next time
                pkl_file = open(pickle_filename, 'wb+')
                pickle.dump(pg_procs_current, pkl_file)
                pkl_file.close()
                
                # And the results
                pg_idle_proc_count=0
                for key in pg_procs_current:
                    if pg_procs_current[key].find('idle in transaction') != -1:
                        pg_idle_proc_count += 1
                        print '{"result":{"test":"pg_idle_in_trans", \
                                          "instance":"pid_' + str(key) +'", \
                                          "result":"fail"}}'
                    else:
                        print '{"result":{"test":"pg_idle_in_trans", \
                                           "instance":"pid_' + str(key) + '", \
                                           "result":"pass"}}'

                print '{"result":{"test":"pg_idle_proc_count", \
                                  "value":' + str(pg_idle_proc_count) + '}}'
                                  
                print '{"result":{"test":"pg_proc_count", \
                                  "value":' + str(len(pg_procs_current)) + '}}'
                                  
                print '{"result":{"test":"pg_poll_processes", \
                                  "result":"ignore"}}'
        if options.instancename:
            print "Instance", options.instancename

main()