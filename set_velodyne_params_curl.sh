# curl http://192.168.0.201/cgi/diag.json
# sleep 1
# curl http://192.168.0.201/cgi/status.json
# sleep 1

init_rpm=600
rpm_desired=300

if [ $init_rpm -gt $rpm_desired ]
    then
        echo "Stepping LiDAR down to ${rpm_desired} from ${init_rpm}..."
        for ((rpm=$init_rpm; rpm>=$rpm_desired; rpm=rpm-60))
            do
                command_rpm="rpm=${rpm}"
                curl --data ${command_rpm} http://192.168.1.201/cgi/setting
                echo
                echo ${command_rpm}
                sleep 5
                curl http://192.168.1.201/cgi/status.json
            done
    else
        echo "Stepping LiDAR up to ${rpm_desired} from ${init_rpm}..."
        for ((rpm=$init_rpm; rpm<=$rpm_desired; rpm=rpm+60))
            do
                command_rpm="rpm=${rpm}"
                curl --data ${command_rpm} http://192.168.1.201/cgi/setting
                echo
                echo ${command_rpm}
                sleep 5
                curl http://192.168.1.201/cgi/status.json
            done
fi
sleep 5
curl http://192.168.1.201/cgi/status.json
echo
echo "Done, RPM at ${command_rpm}..."
echo