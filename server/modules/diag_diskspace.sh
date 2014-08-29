#/bin/bash
#
# Example swdiag server module written in shell script. This simple script
# monitors the free disk space available on this Unix based machine. When
# the disk space drops under the threshold THRESHOLD an email is sent to
# the alert email address.
#
# TODO Only works on Linux for now due to looking for FS type ext.
#

# Read in the configuration
. $0.conf

DISKS=`df -lPT | grep ext | awk '{print $7}'`

if [ -z "$THRESHOLD" ]; then
    echo "COULD NOT READ MODULE CONFIGURATION $0.conf"
    exit
fi

if [ "$1" == "--conf" ]; then
cat <<EOF
["comp":{"name":"Diskspace"},

"test":{"name":"diskspace_low_free_test",
		"polled" : true,
        "interval" : "fast",
        "comp":"Diskspace"},

"rule":{"name":"diskspace_low_free_rule",
		"input":"diskspace_low_free_test",
		"operator":"SWDIAG_RULE_LESS_THAN_N",
		"n":$THRESHOLD,
		"comp":"Diskspace"},

"rule":{"name":"diskspace_low_free_rule_time",
        "input":"diskspace_low_free_rule",
        "action":"diskspace_notification",
        "operator":"SWDIAG_RULE_N_IN_ROW"
        "n":3,
        "comp":"Diskspace"},
EOF

for disk in $DISKS; do
cat <<EOF
"email":{"name":"diskspace_notification",
         "instance":"$disk",
         "subject":"Free disk space is low on this disk"},
          
"instance" : {"object":"diskspace_low_free_test",
              "name":"$disk"},

"instance" : {"object":"diskspace_low_free_rule",
              "name":"$disk"},
              
"instance" : {"object":"diskspace_low_free_rule_time",
              "name":"$disk"},
EOF
done

cat <<EOF
"ready":["diskspace_low_free_test"]
]

EOF
fi
if [ "$1" == "--test" ]; then
	if [ "$2" == "diskspace_low_free_test" ]; then
		if [ "$#" == "2" ]; then
		  
			cat <<EOF
"result":{"test":"diskspace_low_free_test",
			"result":"ignore"},
EOF
            for disk in $DISKS; do
            value=`df -lPT $disk | grep ext | awk '{print $5}'`
            cat <<EOF
"result":{"test":"diskspace_low_free_test",
			"instance":"$disk",
			"value":$value},
EOF
            done
			exit 0
		elif [ "$#" == "4" ]; then
			instance=$4
cat <<EOF
"result":{"test":"diskspace_low_free_test",
			"instance":"$instance",
			"result":"ignore"}
EOF
		fi
		exit 0
    fi
fi

