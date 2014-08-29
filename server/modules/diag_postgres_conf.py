#
# Configuration for postgres diagnostics module
#

# idle_n is the number of test runs that a psql process must
# be idle in transaction before being marked as bad.
idle_n=5

# Total number of processes that are idle in transaction at this
# moment in time.
warning_pg_idle_count=10
critical_pg_idle_count=20

#
# Total Number of postgresql processes
#
warning_pg_count=90
critical_pg_count=120

#
# Health of component drops under 50% for 3 periods
# (Note that health 1000 = 100.0%)
critical_health=800
health_time_n=3
